//
//  VoodooI2CNativeEngine.cpp
//  VoodooI2C
//
//  Created by Alexandre on 10/02/2018.
//  Copyright © 2018 Alexandre Daoud and Kishor Prins. All rights reserved.
//

#include "VoodooI2CNativeEngine.hpp"

#define super VoodooI2CMultitouchEngine
OSDefineMetaClassAndStructors(VoodooI2CNativeEngine, VoodooI2CMultitouchEngine);

MultitouchReturn VoodooI2CNativeEngine::handleInterruptReport(VoodooI2CMultitouchEvent event, AbsoluteTime timestamp) {
    if (!voodooInputInstance) {
        return MultitouchReturnContinue;
    }

    if (!event.transducers)
        return MultitouchReturnBreak;

    auto* firstTransducer = static_cast<VoodooI2CDigitiserTransducer*>(event.transducers->getObject(0));
    if (!firstTransducer)
        return MultitouchReturnBreak;

    const unsigned int firstFinger = firstTransducer->type == kDigitiserTransducerStylus ? 1 : 0;
    const unsigned int available = event.transducers->getCount() - firstFinger;
    unsigned int count = event.contact_count;
    if (count > available)
        count = available;
    if (count > VOODOO_INPUT_MAX_TRANSDUCERS)
        count = VOODOO_INPUT_MAX_TRANSDUCERS;

    message.timestamp = timestamp;
    message.contact_count = static_cast<UInt8>(count);
    memset(message.transducers, 0, sizeof(message.transducers));

    const bool buttonDown = firstTransducer->physical_button.value() != 0;
    const bool emulateForceClick = count && buttonDown && !firstTransducer->has_secondary_button && isForceClickEnabled();
    int valid_touch_count = 0;

    for (unsigned int i = 0; i < count; ++i) {
        auto* transducer = static_cast<VoodooI2CDigitiserTransducer*>(event.transducers->getObject(firstFinger + i));
        VoodooInputTransducer* inputTransducer = &message.transducers[i];
        
        if (!transducer) {
            continue;
        }
        
        inputTransducer->fingerType = (MT2FingerType) (kMT2FingerTypeIndexFinger + (i % 4));
        inputTransducer->secondaryId = transducer->secondary_id;
        
        inputTransducer->type = (transducer->type == DigitiserTransducerType::kDigitiserTransducerFinger) ? VoodooInputTransducerType::FINGER : VoodooInputTransducerType::STYLUS;
        
        inputTransducer->isValid = transducer->is_valid;
        if (inputTransducer->isValid) {
            valid_touch_count++;
        }
        
        if (!transducer->confidence.value()) {
            inputTransducer->fingerType = kMT2FingerTypePalm;
        }
        
        inputTransducer->isTransducerActive = transducer->tip_switch.value();
        inputTransducer->isPhysicalButtonDown = (transducer->physical_button.value() & 0x1) != 0;
        
        inputTransducer->currentCoordinates.x = transducer->coordinates.x.value();
        inputTransducer->previousCoordinates.x = transducer->coordinates.x.last.value;
        
        inputTransducer->currentCoordinates.y = transducer->coordinates.y.value();
        inputTransducer->previousCoordinates.y = transducer->coordinates.y.last.value;
        inputTransducer->supportsPressure = false;
        inputTransducer->timestamp = timestamp;

        // TODO: does VoodooI2C know width(s)? how does it measure pressure?
        inputTransducer->currentCoordinates.width = transducer->tip_pressure.value() / 2;
        inputTransducer->previousCoordinates.width = transducer->tip_pressure.last.value / 2;

        inputTransducer->currentCoordinates.pressure = transducer->tip_pressure.value();
        inputTransducer->previousCoordinates.pressure = transducer->tip_pressure.last.value;

        // Force Touch emulation
        // The button state is saved in the first transducer
        // Disable Force Touch emulation for two-button trackpads
        if (emulateForceClick) {
            inputTransducer->supportsPressure = true;
            inputTransducer->isPhysicalButtonDown = false;
            inputTransducer->currentCoordinates.pressure = 0xff;
            inputTransducer->currentCoordinates.width = 10;
        }
    }
    
    // set the thumb to improve 4F pinch and spread gesture and cross-screen dragging
    if (count && (valid_touch_count >= 4 || buttonDown)) {
        // simple thumb detection: to find the lowest finger touch in the vertical direction.
        UInt32 y_max = 0;
        unsigned int thumb_index = 0;
        for (unsigned int i = 0; i < count; ++i) {
            VoodooInputTransducer* inputTransducer = &message.transducers[i];
            if (inputTransducer->isValid && inputTransducer->currentCoordinates.y >= y_max) {
                y_max = inputTransducer->currentCoordinates.y;
                thumb_index = i;
            }
        }
        message.transducers[thumb_index].fingerType = kMT2FingerTypeThumb;
    }
        
    super::messageClient(kIOMessageVoodooInputMessage, voodooInputInstance, &message, sizeof(VoodooInputEvent));
    
    if (firstTransducer->has_secondary_button) {
        RelativePointerEvent buttonEvent;
        buttonEvent.dx = 0;
        buttonEvent.dy = 0;
        buttonEvent.buttons = event.external_buttons;
        buttonEvent.timestamp = timestamp;
        super::messageClient(kIOMessageVoodooTrackpointRelativePointer, voodooInputInstance, &buttonEvent, sizeof(buttonEvent));
    }
    return MultitouchReturnBreak;
}

bool VoodooI2CNativeEngine::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    parentProvider = OSDynamicCast(VoodooI2CMultitouchInterface, provider);
    
    if (!parentProvider) {
        return false;
    }
    
    voodooInputInstance = nullptr;
    
    setProperty(VOODOO_INPUT_LOGICAL_MAX_X_KEY, parentProvider->logical_max_x, 32);
    setProperty(VOODOO_INPUT_LOGICAL_MAX_Y_KEY, parentProvider->logical_max_y, 32);
    
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_X_KEY, parentProvider->physical_max_x, 32);
    setProperty(VOODOO_INPUT_PHYSICAL_MAX_Y_KEY, parentProvider->physical_max_y, 32);

    setProperty("VoodooInputSupported", kOSBooleanTrue);
    
    nextForceClickPropertyLookup = 0;

    return true;
}

bool VoodooI2CNativeEngine::isForceClickEnabled() {
    AbsoluteTime now;
    clock_get_uptime(&now);
    if (now < nextForceClickPropertyLookup)
        return lastIsForceClickEnabled;

    AbsoluteTime interval;
    nanoseconds_to_absolutetime(1000000000ULL, &interval);
    nextForceClickPropertyLookup = now + interval;

    OSObject* preferences = copyProperty("MultitouchPreferences", gIOServicePlane, kIORegistryIterateRecursively);
    if (auto* dict = OSDynamicCast(OSDictionary, preferences)) {
        OSObject* clicking = dict->getObject("Clicking");
        auto* clickingBool = OSDynamicCast(OSBoolean, clicking);
        auto* clickingNumber = OSDynamicCast(OSNumber, clicking);
        if ((clickingBool && !clickingBool->getValue()) ||
            (clickingNumber && !clickingNumber->unsigned64BitValue())) {
            lastIsForceClickEnabled = false;
        } else if (auto* suppressed = OSDynamicCast(OSBoolean, dict->getObject("ForceSuppressed"))) {
            lastIsForceClickEnabled = !suppressed->getValue();
        }
    }
    OSSafeReleaseNULL(preferences);
    return lastIsForceClickEnabled;
}

void VoodooI2CNativeEngine::onPropertyChange() {
    if (voodooInputInstance) {
        super::messageClient(kIOMessageVoodooInputUpdatePropertiesNotification, voodooInputInstance);
    }
}

void VoodooI2CNativeEngine::stop(IOService* provider) {
    super::stop(provider);
}

bool VoodooI2CNativeEngine::handleOpen(IOService *forClient, IOOptionBits options, void *arg) {
    if (forClient && forClient->getProperty(VOODOO_INPUT_IDENTIFIER)) {
        voodooInputInstance = forClient;
        voodooInputInstance->retain();
        
        return true;
    }
    return false;
}

bool VoodooI2CNativeEngine::handleIsOpen(const IOService *forClient) const {
    return voodooInputInstance != nullptr && forClient == voodooInputInstance;
}

void VoodooI2CNativeEngine::handleClose(IOService *forClient, IOOptionBits options) {
    if (voodooInputInstance && forClient == voodooInputInstance) {
        OSSafeReleaseNULL(voodooInputInstance);
    }
}

bool VoodooI2CNativeEngine::willTerminate(IOService *provider, IOOptionBits options) {
    return super::willTerminate(provider, options);
}

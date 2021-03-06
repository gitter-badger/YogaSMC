//  SPDX-License-Identifier: GPL-2.0-only
//
//  IdeaVPC.cpp
//  YogaSMC
//
//  Created by Zhen on 2020/7/11.
//  Copyright © 2020 Zhen. All rights reserved.
//

#include "IdeaVPC.hpp"
OSDefineMetaClassAndStructors(IdeaVPC, YogaVPC);

IdeaVPC* IdeaVPC::withDevice(IOACPIPlatformDevice *device, OSString *pnp) {
    IdeaVPC* vpc = OSTypeAlloc(IdeaVPC);

    OSDictionary* dictionary = OSDictionary::withCapacity(1);
    dictionary->setObject("Feature", pnp);

    vpc->vpc = device;

    if (!vpc->init(dictionary) ||
        !vpc->attach(device) ||
        !vpc->start(device)) {
        OSSafeReleaseNULL(vpc);
    }

    dictionary->release();

    return vpc;
}

void IdeaVPC::updateAll() {
    updateConservation();
    updateFnlock();
    updateVPC();
}

bool IdeaVPC::initVPC() {
    if (vpc->evaluateInteger(getVPCConfig, &config) != kIOReturnSuccess) {
        IOLog(updateFailure, getName(), VPCPrompt);
        return false;
    }

    IOLog(updateSuccess, getName(), VPCPrompt, config);
#ifdef DEBUG
    setProperty(VPCPrompt, config, 32);
#endif
    cap_graphics = config >> CFG_GRAPHICS_BIT & 0x7;
    cap_bt       = config >> CFG_BT_BIT & 0x1;
    cap_3g       = config >> CFG_3G_BIT & 0x1;
    cap_wifi     = config >> CFG_WIFI_BIT & 0x1;
    cap_camera   = config >> CFG_CAMERA_BIT & 0x1;

#ifdef DEBUG
    OSDictionary *capabilities = OSDictionary::withCapacity(5);
    OSObject *value;

    switch (cap_graphics) {
        case 1:
            value = OSString::withCString("Intel");
            break;

        case 2:
            value = OSString::withCString("ATI");
            break;

        case 3:
            value = OSString::withCString("Nvidia");
            break;

        case 4:
            value = OSString::withCString("Intel and ATI");
            break;

        case 5:
            value = OSString::withCString("Intel and Nvidia");
            break;

        default:
            char Unknown[10];
            snprintf(Unknown, 10, "Unknown:%1x", cap_graphics);
            value = OSString::withCString(Unknown);
            break;
    }
    capabilities->setObject("Graphics", value);
    value->release();
    capabilities->setObject("Bluetooth", cap_bt ? kOSBooleanTrue : kOSBooleanFalse);
    capabilities->setObject("3G", cap_3g ? kOSBooleanTrue : kOSBooleanFalse);
    capabilities->setObject("Wireless", cap_wifi ? kOSBooleanTrue : kOSBooleanFalse);
    capabilities->setObject("Camera", cap_camera ? kOSBooleanTrue : kOSBooleanFalse);

    setProperty("Capability", capabilities);
    capabilities->release();
#endif
    updateAll();
    return true;
}

void IdeaVPC::setPropertiesGated(OSObject *props) {
    if (!vpc) {
        IOLog(VPCUnavailable, getName());
        return;
    }

    OSDictionary* dict = OSDynamicCast(OSDictionary, props);
    if (!dict)
        return;

//    IOLog("%s: %d objects in properties\n", getName(), dict->getCount());
    OSCollectionIterator* i = OSCollectionIterator::withCollection(dict);

    if (i != NULL) {
        while (OSString* key = OSDynamicCast(OSString, i->getNextObject())) {
            if (key->isEqualTo(conservationPrompt)) {
                OSBoolean * value = OSDynamicCast(OSBoolean, dict->getObject(conservationPrompt));
                if (value == NULL) {
                    IOLog(valueInvalid, getName(), conservationPrompt);
                    continue;
                }

                updateConservation(false);

                if (value->getValue() == conservationMode) {
                    IOLog(valueMatched, getName(), conservationPrompt, (conservationMode ? "enabled" : "disabled"));
                } else {
                    toggleConservation();
                }
            } else if (key->isEqualTo(FnKeyPrompt)) {
                OSBoolean * value = OSDynamicCast(OSBoolean, dict->getObject(FnKeyPrompt));
                if (value == NULL) {
                    IOLog(valueInvalid, getName(), FnKeyPrompt);
                    continue;
                }

                updateFnlock(false);

                if (value->getValue() == FnlockMode) {
                    IOLog(valueMatched, getName(), FnKeyPrompt, (FnlockMode ? "enabled" : "disabled"));
                } else {
                    toggleFnlock();
                }
            } else if (key->isEqualTo(readECPrompt)) {
                OSNumber * value = OSDynamicCast(OSNumber, dict->getObject(readECPrompt));
                if (value == NULL) {
                    IOLog(valueInvalid, getName(), readECPrompt);
                    continue;
                }

                UInt32 result;
                UInt8 retries = 0;

                if (read_ec_data(value->unsigned32BitValue(), &result, &retries))
                    IOLog("%s: %s 0x%x result: 0x%x %d\n", getName(), readECPrompt, value->unsigned32BitValue(), result, retries);
                else
                    IOLog("%s: %s failed 0x%x %d\n", getName(), readECPrompt, value->unsigned32BitValue(), retries);
            } else if (key->isEqualTo(writeECPrompt)) {
                OSNumber * value = OSDynamicCast(OSNumber, dict->getObject(writeECPrompt));
                if (value == NULL) {
                    IOLog(valueInvalid, getName(), writeECPrompt);
                    continue;
                }
                IOLog("%s: %s 0x%x not implemented\n", getName(), writeECPrompt, value->unsigned32BitValue());
            } else if (key->isEqualTo(clamshellPrompt)) {
                OSDictionary* entry = OSDictionary::withCapacity(1);
                entry->setObject(clamshellPrompt, dict->getObject(clamshellPrompt));
                super::setPropertiesGated(entry);
                entry->release();
            } else if (key->isEqualTo(updatePrompt)) {
                updateAll();
                super::updateAll();
            } else {
                IOLog("%s: Unknown property %s\n", getName(), key->getCStringNoCopy());
            }
        }
        i->release();
    }

    return;
}


bool IdeaVPC::updateConservation(bool update) {
    UInt32 state;

    if (vpc->evaluateInteger(getConservationMode, &state) != kIOReturnSuccess) {
        IOLog(updateFailure, getName(), conservationPrompt);
        return false;
    }

    conservationMode = 1 << BM_CONSERVATION_BIT & state;

    if (update) {
        IOLog(updateSuccess, getName(), conservationPrompt, state);
        setProperty(conservationPrompt, conservationMode);
    }

    return true;
}

bool IdeaVPC::updateFnlock(bool update) {
    UInt32 state;

    if (vpc->evaluateInteger(getFnlockMode, &state) != kIOReturnSuccess) {
        IOLog(updateFailure, getName(), FnKeyPrompt);
        return false;
    }

    FnlockMode = 1 << HA_FNLOCK_BIT & state;

    if (update) {
        IOLog(updateSuccess, getName(), FnKeyPrompt, state);
        setProperty(FnKeyPrompt, FnlockMode);
    }

    return true;
}

bool IdeaVPC::toggleConservation() {
    UInt32 result;

    OSObject* params[1] = {
        OSNumber::withNumber((!conservationMode ? BMCMD_CONSERVATION_ON : BMCMD_CONSERVATION_OFF), 32)
    };

    if (vpc->evaluateInteger(setConservationMode, &result, params, 1) != kIOReturnSuccess || result != 0) {
        IOLog(toggleFailure, getName(), conservationPrompt);
        return false;
    }

    conservationMode = !conservationMode;
    IOLog(toggleSuccess, getName(), conservationPrompt, (conservationMode ? BMCMD_CONSERVATION_ON : BMCMD_CONSERVATION_OFF), (conservationMode ? "on" : "off"));
    setProperty(conservationPrompt, conservationMode);
    // TODO: sync with system preference

    return true;
}

bool IdeaVPC::toggleFnlock() {
    UInt32 result;

    OSObject* params[1] = {
        OSNumber::withNumber((!FnlockMode ? HACMD_FNLOCK_ON : HACMD_FNLOCK_OFF), 32)
    };

    if (vpc->evaluateInteger(setFnlockMode, &result, params, 1) != kIOReturnSuccess || result != 0) {
        IOLog(toggleFailure, getName(), FnKeyPrompt);
        return false;
    }

    FnlockMode = !FnlockMode;
    IOLog(toggleSuccess, getName(), FnKeyPrompt, (FnlockMode ? HACMD_FNLOCK_ON : HACMD_FNLOCK_OFF), (FnlockMode ? "on" : "off"));
    setProperty(FnKeyPrompt, FnlockMode);

    return true;
}

void IdeaVPC::updateVPC() {
    UInt32 vpc1, vpc2, result;
    UInt8 retries = 0;

    if (!read_ec_data(VPCCMD_R_VPC1, &vpc1, &retries) || !read_ec_data(VPCCMD_R_VPC2, &vpc2, &retries)) {
        IOLog("%s: Failed to read VPC %d\n", getName(), retries);
        return;
    }

    vpc1 = (vpc2 << 8) | vpc1;
#ifdef DEBUG
    IOLog("%s: read VPC EC result: 0x%x %d\n", getName(), vpc1, retries);
    setProperty("VPCstatus", vpc1, 32);
#endif
    for (int vpc_bit = 0; vpc_bit < 16; vpc_bit++) {
        if (1 << vpc_bit & vpc1) {
            switch (vpc_bit) {
                case 0:
                    if (!read_ec_data(VPCCMD_R_SPECIAL_BUTTONS, &result, &retries)) {
                        IOLog("%s: Failed to read VPCCMD_R_SPECIAL_BUTTONS %d\n", getName(), retries);
                    } else {
                        switch (result) {
                            case 0x40:
                                IOLog("%s: Fn+Q cooling", getName());
                                // TODO: fan status switch
                                break;

                            default:
                                IOLog("%s: Special button 0x%x", getName(), result);
                                break;
                        }
                    }
                    break;

                case 1:
                    IOLog("%s: Fn+Space keyboard backlight?", getName());
                    // functional, TODO: read / write keyboard backlight level
                    // also on AC connect / disconnect
                    break;

                case 2:
                    if (!read_ec_data(VPCCMD_R_BL_POWER, &result, &retries))
                        IOLog("%s: Failed to read VPCCMD_R_BL_POWER %d\n", getName(), retries);
                    else
                        IOLog("%s: Open lid? 0x%x %s", getName(), result, result ? "on" : "off");
                    // functional, TODO: turn off screen on demand
                    break;

                case 5:
                    if (!read_ec_data(VPCCMD_R_TOUCHPAD, &result, &retries))
                        IOLog("%s: Failed to read VPCCMD_R_TOUCHPAD %d\n", getName(), retries);
                    else
                        IOLog("%s: Fn+F6 touchpad 0x%x %s", getName(), result, result ? "on" : "off");
                    // functional, TODO: manually toggle
                    break;

                case 7:
                    IOLog("%s: Fn+F8 camera", getName());
                    // TODO: camera status switch
                    break;

                case 8:
                    IOLog("%s: Fn+F4 mic", getName());
                    // TODO: mic status switch
                    break;

                case 10:
                    IOLog("%s: Fn+F6 touchpad on", getName());
                    // functional, identical to case 5?
                    break;

                case 13:
                    IOLog("%s: Fn+F7 airplane mode", getName());
                    // TODO: airplane mode switch
                    break;

                default:
                    IOLog("%s: Unknown VPC event %d", getName(), vpc_bit);
                    break;
            }
        }
    }
}

bool IdeaVPC::read_ec_data(UInt32 cmd, UInt32 *result, UInt8 *retries) {
    if (!vpc) {
        IOLog(VPCUnavailable, getName());
        return false;
    }

    if (!method_vpcw(1, cmd))
        return false;

    AbsoluteTime abstime, deadline, now_abs;
    nanoseconds_to_absolutetime(IDEAPAD_EC_TIMEOUT * 1000 * 1000, &abstime);
    clock_absolutetime_interval_to_deadline(abstime, &deadline);

    do {
        if (!method_vpcr(1, result))
            return false;

        if (*result == 0)
            return method_vpcr(0, result);

        *retries = *retries + 1;
        IODelay(250);
        clock_get_uptime(&now_abs);
    } while (now_abs < deadline || *retries < 5);

    IOLog(timeoutPrompt, getName(), readECPrompt, cmd);
    return false;
}

bool IdeaVPC::write_ec_data(UInt32 cmd, UInt32 value, UInt8 *retries) {
    if (!vpc) {
        IOLog(VPCUnavailable, getName());
        return false;
    }

    UInt32 result;

    if (!method_vpcw(0, value))
        return false;

    if (!method_vpcw(1, cmd))
        return false;

    AbsoluteTime abstime, deadline, now_abs;
    nanoseconds_to_absolutetime(IDEAPAD_EC_TIMEOUT * 1000 * 1000, &abstime);
    clock_absolutetime_interval_to_deadline(abstime, &deadline);

    do {
        if (!method_vpcr(1, &result))
            return false;

        if (result == 0)
            return true;

        *retries = *retries + 1;
        IODelay(250);
        clock_get_uptime(&now_abs);
    } while (now_abs < deadline || *retries < 5);

    IOLog(timeoutPrompt, getName(), writeECPrompt, cmd);
    return false;
}

bool IdeaVPC::method_vpcr(UInt32 cmd, UInt32 *result) {
    OSObject* params[1] = {
        OSNumber::withNumber(cmd, 32)
    };

    return (vpc->evaluateInteger(readVPCStatus, result, params, 1) == kIOReturnSuccess);
}

bool IdeaVPC::method_vpcw(UInt32 cmd, UInt32 data) {
    UInt32 result; // should only return 0

    OSObject* params[2] = {
        OSNumber::withNumber(cmd, 32),
        OSNumber::withNumber(data, 32)
    };

    return (vpc->evaluateInteger(writeVPCStatus, &result, params, 2) == kIOReturnSuccess);
}

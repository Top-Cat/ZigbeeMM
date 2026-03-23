import {Zcl} from "zigbee-herdsman";
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['MMWave Sensor'],
    model: 'MMWave Sensor',
    vendor: 'TC',
    icon: 'device_icons/8783691.png',
    description: 'Custom MMWave Sensor',
    extend: [
        m.deviceAddCustomCluster("tcSpecificMmwave", {
            manufacturerCode: 0x1234,
            ID: 0xFC10,
            attributes: {
                'bluetooth': { ID: 0x0001, type: Zcl.DataType.BOOLEAN, write: true },
                'debug1': { ID: 0x00F0, type: Zcl.DataType.UINT16, write: true, max: 0xffff },
                'debug2': { ID: 0x00F1, type: Zcl.DataType.UINT16, write: true, max: 0xffff },
                'debug3': { ID: 0x00F2, type: Zcl.DataType.UINT16, write: true, max: 0xffff },
                'debug4': { ID: 0x00F3, type: Zcl.DataType.UINT16, write: true, max: 0xffff },
                'debug5': { ID: 0x00F4, type: Zcl.DataType.UINT16, write: true, max: 0xffff },
                'debug6': { ID: 0x00F5, type: Zcl.DataType.UINT16, write: true, max: 0xffff}
            },
            commands: {},
            commandsResponse: {},
        }),
        m.occupancy({
            pirConfig: ["otu_delay"]
        }),
        m.identify(),
        {
            ...(m.illuminance({
                reporting: {min: 10, max: 600, change: 200}
            })),
            options: [
                options.precision("illuminance")
            ]
        },
        m.temperature(),
        m.binary({
            name: 'bluetooth',
            valueOn: ['ON', 1],
            valueOff: ['OFF', 0],
            cluster: 'tcSpecificMmwave',
            attribute: 'bluetooth',
            description: 'Enable bluetooth'
        })
    ],
    ota: true
};

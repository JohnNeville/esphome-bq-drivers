import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, i2c, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_ENERGY_STORAGE,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_MINUTE,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import bq34z100_ns

DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "button"]

UNIT_MILLIAMPERE = "mA"
UNIT_MILLIAMP_HOURS = "mAh"
UNIT_MILLIOHM = "mΩ"

# Data flash configuration
CONF_APPLY_CONFIGURATION = "apply_configuration"
CONF_UNSEAL_KEY_0 = "unseal_key_0"
CONF_UNSEAL_KEY_1 = "unseal_key_1"
CONF_DESIGN_CAPACITY = "design_capacity"
CONF_DESIGN_ENERGY = "design_energy"
CONF_CELL_COUNT = "cell_count"
CONF_SENSE_RESISTOR = "sense_resistor"
CONF_VOLTAGE_DIVIDER = "voltage_divider"
CONF_EXTERNAL_THERMISTOR = "external_thermistor"
CONF_LIFEPO4_RELAX = "lifepo4_relax"
CONF_EXPECTED_CHEM_ID = "expected_chem_id"

# Sensors
CONF_STATE_OF_CHARGE = "state_of_charge"
CONF_VOLTAGE = "voltage"
CONF_CURRENT = "current"
CONF_AVERAGE_CURRENT = "average_current"
CONF_AVERAGE_POWER = "average_power"
CONF_TEMPERATURE = "temperature"
CONF_INTERNAL_TEMPERATURE = "internal_temperature"
CONF_REMAINING_CAPACITY = "remaining_capacity"
CONF_FULL_CHARGE_CAPACITY = "full_charge_capacity"
CONF_AVAILABLE_ENERGY = "available_energy"
CONF_TIME_TO_EMPTY = "time_to_empty"
CONF_TIME_TO_FULL = "time_to_full"
CONF_CYCLE_COUNT = "cycle_count"
CONF_STATE_OF_HEALTH = "state_of_health"
CONF_CHEM_ID = "chem_id"

# Binary sensors
CONF_DISCHARGING = "discharging"
CONF_FULLY_CHARGED = "fully_charged"
CONF_CHARGE_INHIBITED = "charge_inhibited"
CONF_LOW_CHARGE = "low_charge"
CONF_CRITICAL_CHARGE = "critical_charge"
CONF_BATTERY_HIGH = "battery_high"
CONF_BATTERY_LOW = "battery_low"
CONF_OVER_TEMPERATURE = "over_temperature"

# Text sensor
CONF_STATUS = "status"

# Buttons
CONF_CC_OFFSET_CALIBRATION = "cc_offset_calibration"
CONF_BOARD_OFFSET_CALIBRATION = "board_offset_calibration"
CONF_ENABLE_IMPEDANCE_TRACK = "enable_impedance_track"
CONF_RESET_GAUGE = "reset_gauge"
CONF_DUMP_DATA_FLASH = "dump_data_flash"

BQ34Z100Component = bq34z100_ns.class_(
    "BQ34Z100Component", cg.PollingComponent, i2c.I2CDevice
)
BQ34Z100CcOffsetButton = bq34z100_ns.class_("BQ34Z100CcOffsetButton", button.Button, cg.Component)
BQ34Z100BoardOffsetButton = bq34z100_ns.class_("BQ34Z100BoardOffsetButton", button.Button, cg.Component)
BQ34Z100ItEnableButton = bq34z100_ns.class_("BQ34Z100ItEnableButton", button.Button, cg.Component)
BQ34Z100ResetButton = bq34z100_ns.class_("BQ34Z100ResetButton", button.Button, cg.Component)
BQ34Z100DumpButton = bq34z100_ns.class_("BQ34Z100DumpButton", button.Button, cg.Component)


def _capacity_sensor():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLIAMP_HOURS,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _current_sensor():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_MILLIAMPERE,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_CURRENT,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _temperature_sensor():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _duration_sensor():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_MINUTE,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_DURATION,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


def _percent_sensor(device_class=None):
    kwargs = {
        "unit_of_measurement": UNIT_PERCENT,
        "accuracy_decimals": 0,
        "state_class": STATE_CLASS_MEASUREMENT,
    }
    # sensor_schema treats an explicit None as a value rather than "unset",
    # so the key is only passed when there is a device class to pass.
    if device_class is not None:
        kwargs["device_class"] = device_class
    return sensor.sensor_schema(**kwargs)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BQ34Z100Component),
            # --- Data flash configuration ---
            cv.Optional(CONF_APPLY_CONFIGURATION, default=False): cv.boolean,
            cv.Optional(CONF_UNSEAL_KEY_0, default=0x0414): cv.hex_uint16_t,
            cv.Optional(CONF_UNSEAL_KEY_1, default=0x3672): cv.hex_uint16_t,
            cv.Optional(CONF_DESIGN_CAPACITY): cv.int_range(min=0, max=32767),
            cv.Optional(CONF_DESIGN_ENERGY): cv.int_range(min=0, max=32767),
            cv.Optional(CONF_CELL_COUNT): cv.int_range(min=1, max=16),
            # Value of the external shunt in milliohms; sets CC Gain / CC Delta.
            cv.Optional(CONF_SENSE_RESISTOR): cv.float_range(min=0.1, max=100.0),
            # Full-scale pack voltage in mV. Setting this also selects the
            # external divider via the VOLTSEL bit in Pack Configuration.
            cv.Optional(CONF_VOLTAGE_DIVIDER): cv.int_range(min=0, max=65535),
            cv.Optional(CONF_EXTERNAL_THERMISTOR, default=True): cv.boolean,
            cv.Optional(CONF_LIFEPO4_RELAX): cv.boolean,
            # Advisory only. There is no I2C command that sets the chemistry,
            # so a mismatch is reported rather than corrected.
            cv.Optional(CONF_EXPECTED_CHEM_ID): cv.hex_uint16_t,
            # --- Sensors ---
            cv.Optional(CONF_STATE_OF_CHARGE): _percent_sensor(DEVICE_CLASS_BATTERY),
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): _current_sensor(),
            cv.Optional(CONF_AVERAGE_CURRENT): _current_sensor(),
            cv.Optional(CONF_AVERAGE_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TEMPERATURE): _temperature_sensor(),
            cv.Optional(CONF_INTERNAL_TEMPERATURE): _temperature_sensor(),
            cv.Optional(CONF_REMAINING_CAPACITY): _capacity_sensor(),
            cv.Optional(CONF_FULL_CHARGE_CAPACITY): _capacity_sensor(),
            cv.Optional(CONF_AVAILABLE_ENERGY): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT_HOURS,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_ENERGY_STORAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TIME_TO_EMPTY): _duration_sensor(),
            cv.Optional(CONF_TIME_TO_FULL): _duration_sensor(),
            cv.Optional(CONF_CYCLE_COUNT): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:battery-sync",
            ),
            cv.Optional(CONF_STATE_OF_HEALTH): _percent_sensor(),
            cv.Optional(CONF_CHEM_ID): sensor.sensor_schema(
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:flask-outline",
            ),
            # --- Binary sensors (decoded from Flags(), 0x0E) ---
            cv.Optional(CONF_DISCHARGING): binary_sensor.binary_sensor_schema(
                device_class="battery_charging",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_FULLY_CHARGED): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:battery-check",
            ),
            cv.Optional(CONF_CHARGE_INHIBITED): binary_sensor.binary_sensor_schema(
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_LOW_CHARGE): binary_sensor.binary_sensor_schema(
                device_class="battery",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CRITICAL_CHARGE): binary_sensor.binary_sensor_schema(
                device_class="battery",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BATTERY_HIGH): binary_sensor.binary_sensor_schema(
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
                device_class="problem",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_OVER_TEMPERATURE): binary_sensor.binary_sensor_schema(
                device_class="heat",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # --- Text sensor ---
            cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:battery-heart-variant",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # --- Buttons ---
            cv.Optional(CONF_CC_OFFSET_CALIBRATION): button.button_schema(
                BQ34Z100CcOffsetButton,
                icon="mdi:tune-variant",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BOARD_OFFSET_CALIBRATION): button.button_schema(
                BQ34Z100BoardOffsetButton,
                icon="mdi:tune-variant",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_ENABLE_IMPEDANCE_TRACK): button.button_schema(
                BQ34Z100ItEnableButton,
                icon="mdi:chart-bell-curve",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_RESET_GAUGE): button.button_schema(
                BQ34Z100ResetButton,
                icon="mdi:restart-alert",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Logs every documented data flash subclass as hex. Used to
            # capture a golden image from a gauge programmed with bqStudio.
            cv.Optional(CONF_DUMP_DATA_FLASH): button.button_schema(
                BQ34Z100DumpButton,
                icon="mdi:content-save-outline",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x55)),
)

SENSOR_SETTERS = {
    CONF_STATE_OF_CHARGE: "set_state_of_charge_sensor",
    CONF_VOLTAGE: "set_voltage_sensor",
    CONF_CURRENT: "set_current_sensor",
    CONF_AVERAGE_CURRENT: "set_average_current_sensor",
    CONF_AVERAGE_POWER: "set_average_power_sensor",
    CONF_TEMPERATURE: "set_temperature_sensor",
    CONF_INTERNAL_TEMPERATURE: "set_internal_temperature_sensor",
    CONF_REMAINING_CAPACITY: "set_remaining_capacity_sensor",
    CONF_FULL_CHARGE_CAPACITY: "set_full_charge_capacity_sensor",
    CONF_AVAILABLE_ENERGY: "set_available_energy_sensor",
    CONF_TIME_TO_EMPTY: "set_time_to_empty_sensor",
    CONF_TIME_TO_FULL: "set_time_to_full_sensor",
    CONF_CYCLE_COUNT: "set_cycle_count_sensor",
    CONF_STATE_OF_HEALTH: "set_state_of_health_sensor",
    CONF_CHEM_ID: "set_chem_id_sensor",
}

BINARY_SENSOR_SETTERS = {
    CONF_DISCHARGING: "set_discharging_sensor",
    CONF_FULLY_CHARGED: "set_fully_charged_sensor",
    CONF_CHARGE_INHIBITED: "set_charge_inhibited_sensor",
    CONF_LOW_CHARGE: "set_low_charge_sensor",
    CONF_CRITICAL_CHARGE: "set_critical_charge_sensor",
    CONF_BATTERY_HIGH: "set_battery_high_sensor",
    CONF_BATTERY_LOW: "set_battery_low_sensor",
    CONF_OVER_TEMPERATURE: "set_over_temperature_sensor",
}

BUTTON_SETTERS = {
    CONF_CC_OFFSET_CALIBRATION: "set_cc_offset_button",
    CONF_BOARD_OFFSET_CALIBRATION: "set_board_offset_button",
    CONF_ENABLE_IMPEDANCE_TRACK: "set_it_enable_button",
    CONF_RESET_GAUGE: "set_reset_button",
    CONF_DUMP_DATA_FLASH: "set_dump_button",
}


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_apply_configuration(config[CONF_APPLY_CONFIGURATION]))
    cg.add(var.set_unseal_keys(config[CONF_UNSEAL_KEY_0], config[CONF_UNSEAL_KEY_1]))
    cg.add(var.set_external_thermistor(config[CONF_EXTERNAL_THERMISTOR]))

    if CONF_DESIGN_CAPACITY in config:
        cg.add(var.set_design_capacity(config[CONF_DESIGN_CAPACITY]))
    if CONF_DESIGN_ENERGY in config:
        cg.add(var.set_design_energy(config[CONF_DESIGN_ENERGY]))
    if CONF_CELL_COUNT in config:
        cg.add(var.set_cell_count(config[CONF_CELL_COUNT]))
    if CONF_SENSE_RESISTOR in config:
        cg.add(var.set_sense_resistor(config[CONF_SENSE_RESISTOR]))
    if CONF_VOLTAGE_DIVIDER in config:
        cg.add(var.set_voltage_divider(config[CONF_VOLTAGE_DIVIDER]))
    if CONF_LIFEPO4_RELAX in config:
        cg.add(var.set_lifepo4_relax(config[CONF_LIFEPO4_RELAX]))
    if CONF_EXPECTED_CHEM_ID in config:
        cg.add(var.set_expected_chem_id(config[CONF_EXPECTED_CHEM_ID]))

    for key, setter in SENSOR_SETTERS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in BINARY_SENSOR_SETTERS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    if CONF_STATUS in config:
        ts = await text_sensor.new_text_sensor(config[CONF_STATUS])
        cg.add(var.set_status_text_sensor(ts))

    for key, setter in BUTTON_SETTERS.items():
        if key in config:
            btn = await button.new_button(config[key])
            await cg.register_component(btn, config[key])
            cg.add(btn.set_parent(var))
            cg.add(getattr(var, setter)(btn))

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    button,
    i2c,
    number,
    select,
    sensor,
    switch,
    text_sensor,
)
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_FLASH,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_VOLT,
)

DEPENDENCIES = ["i2c"]
AUTO_LOAD = [
    "sensor",
    "binary_sensor",
    "text_sensor",
    "switch",
    "number",
    "button",
    "select",
]

UNIT_MILLIAMPERE = "mA"

# Component-level options
CONF_BATTERY_CAPACITY = "battery_capacity"
CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_CHEMISTRY = "chemistry"
CONF_CELL_COUNT = "cell_count"
CONF_CELL_VOLTAGE = "cell_voltage"
CONF_DEFAULT_CHARGE_CURRENT = "default_charge_current"
CONF_SHIP_FET_PRESENT = "ship_fet_present"
CONF_SHIP_FET_ACTION_DELAY = "ship_fet_action_delay"
CONF_BATTERY_OCP = "battery_ocp"
CONF_WATCHDOG = "watchdog"
CONF_TS_RESISTOR_UPPER = "ts_resistor_upper"
CONF_TS_RESISTOR_LOWER = "ts_resistor_lower"
CONF_TS_NOMINAL_RESISTANCE = "ts_nominal_resistance"
CONF_TS_BETA = "ts_beta"

# Sensors
CONF_VBUS_VOLTAGE = "vbus_voltage"
CONF_VAC1_VOLTAGE = "vac1_voltage"
CONF_VAC2_VOLTAGE = "vac2_voltage"
CONF_VBAT_VOLTAGE = "vbat_voltage"
CONF_VSYS_VOLTAGE = "vsys_voltage"
CONF_IBUS_CURRENT = "ibus_current"
CONF_IBAT_CURRENT = "ibat_current"
CONF_BATTERY_TEMPERATURE = "battery_temperature"
CONF_DIE_TEMPERATURE = "die_temperature"

# Binary sensors
CONF_VBUS_PRESENT = "vbus_present"
CONF_VAC1_PRESENT = "vac1_present"
CONF_VAC2_PRESENT = "vac2_present"
CONF_POWER_GOOD = "power_good"
CONF_CHARGING = "charging"
CONF_BATTERY_PRESENT = "battery_present"
CONF_THERMAL_REGULATION = "thermal_regulation"

# Text sensors
CONF_CHARGE_STATUS = "charge_status"
CONF_INPUT_SOURCE = "input_source"
CONF_CHARGER_FAULT = "charger_fault"

# Switches
CONF_ENABLE_CHARGING = "enable_charging"
CONF_ENABLE_HIZ = "enable_hiz"
CONF_ENABLE_MPPT = "enable_mppt"
CONF_ENABLE_STAT_LED = "enable_stat_led"
CONF_ENABLE_ACDRV1 = "enable_acdrv1"
CONF_ENABLE_ACDRV2 = "enable_acdrv2"
CONF_ENABLE_ICO = "enable_ico"

# Numbers
CONF_CHARGE_CURRENT = "charge_current"
CONF_CHARGE_VOLTAGE = "charge_voltage"
CONF_INPUT_CURRENT_LIMIT = "input_current_limit"
CONF_VINDPM = "vindpm"
CONF_MIN_SYSTEM_VOLTAGE = "min_system_voltage"

# Buttons
CONF_SHIP_MODE = "ship_mode"
CONF_SHUTDOWN = "shutdown"
CONF_POWER_CYCLE = "power_cycle"
CONF_RESET_REGISTERS = "reset_registers"

# Selects
CONF_MPPT_VOC_RATIO = "mppt_voc_ratio"
CONF_MPPT_VOC_DELAY = "mppt_voc_delay"
CONF_MPPT_VOC_RATE = "mppt_voc_rate"

# Option lists are index-ordered to match the REG15 field encodings.
VOC_RATIO_OPTIONS = [
    "0.5625",
    "0.625",
    "0.6875",
    "0.75",
    "0.8125",
    "0.875",
    "0.9375",
    "1.0",
]
VOC_DELAY_OPTIONS = ["50ms", "300ms", "2s", "5s"]
VOC_RATE_OPTIONS = ["30s", "2min", "10min", "30min"]

# Per-cell charge voltage for each supported chemistry, in volts. The charge
# voltage is derived from this rather than exposed as a control, because
# charging a LiFePO4 cell at the 4.2 V the PROG pin selects damages it, and a
# limit that protects the cell should not be something anyone can drag.
CHEMISTRY_CELL_VOLTAGE = {
    "lifepo4": 3.65,
    "li_ion": 4.20,
    "li_ion_4_35": 4.35,
    "li_ion_4_40": 4.40,
    # Takes cell_voltage instead, for a pack none of the above describes.
    "custom": None,
}

# WATCHDOG_2:0 encodings from REG10.
WATCHDOG_OPTIONS = {
    "disabled": 0,
    "0.5s": 1,
    "1s": 2,
    "2s": 3,
    "20s": 4,
    "40s": 5,
    "80s": 6,
    "160s": 7,
}

bq25798_ns = cg.esphome_ns.namespace("bq25798")
BQ25798Component = bq25798_ns.class_(
    "BQ25798Component", cg.PollingComponent, i2c.I2CDevice
)

BQ25798ChargingSwitch = bq25798_ns.class_("BQ25798ChargingSwitch", switch.Switch, cg.Component)
BQ25798HizSwitch = bq25798_ns.class_("BQ25798HizSwitch", switch.Switch, cg.Component)
BQ25798MpptSwitch = bq25798_ns.class_("BQ25798MpptSwitch", switch.Switch, cg.Component)
BQ25798StatLedSwitch = bq25798_ns.class_("BQ25798StatLedSwitch", switch.Switch, cg.Component)
BQ25798Acdrv1Switch = bq25798_ns.class_("BQ25798Acdrv1Switch", switch.Switch, cg.Component)
BQ25798Acdrv2Switch = bq25798_ns.class_("BQ25798Acdrv2Switch", switch.Switch, cg.Component)
BQ25798IcoSwitch = bq25798_ns.class_("BQ25798IcoSwitch", switch.Switch, cg.Component)

BQ25798ChargeCurrentNumber = bq25798_ns.class_("BQ25798ChargeCurrentNumber", number.Number, cg.Component)
BQ25798ChargeVoltageNumber = bq25798_ns.class_("BQ25798ChargeVoltageNumber", number.Number, cg.Component)
BQ25798InputCurrentNumber = bq25798_ns.class_("BQ25798InputCurrentNumber", number.Number, cg.Component)
BQ25798VindpmNumber = bq25798_ns.class_("BQ25798VindpmNumber", number.Number, cg.Component)
BQ25798MinSysVoltageNumber = bq25798_ns.class_("BQ25798MinSysVoltageNumber", number.Number, cg.Component)

BQ25798ShipModeButton = bq25798_ns.class_("BQ25798ShipModeButton", button.Button, cg.Component)
BQ25798ShutdownButton = bq25798_ns.class_("BQ25798ShutdownButton", button.Button, cg.Component)
BQ25798PowerCycleButton = bq25798_ns.class_("BQ25798PowerCycleButton", button.Button, cg.Component)
BQ25798ResetButton = bq25798_ns.class_("BQ25798ResetButton", button.Button, cg.Component)

BQ25798VocRatioSelect = bq25798_ns.class_("BQ25798VocRatioSelect", select.Select, cg.Component)
BQ25798VocDelaySelect = bq25798_ns.class_("BQ25798VocDelaySelect", select.Select, cg.Component)
BQ25798VocRateSelect = bq25798_ns.class_("BQ25798VocRateSelect", select.Select, cg.Component)


def _voltage_sensor():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_VOLTAGE,
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


def _number(cls, unit, min_value, max_value, step):
    return number.number_schema(
        cls,
        unit_of_measurement=unit,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ).extend(
        {
            cv.Optional("min_value", default=min_value): cv.float_,
            cv.Optional("max_value", default=max_value): cv.float_,
            cv.Optional("step", default=step): cv.float_,
        }
    )


def _validate_chemistry(config):
    """The charge voltage limit comes from the chemistry, and from nothing
    else. Allowing a control entity to move it as well would defeat the point
    of deriving it."""
    chemistry = config.get(CONF_CHEMISTRY)

    if chemistry == "custom" and CONF_CELL_VOLTAGE not in config:
        raise cv.Invalid(
            f"'{CONF_CHEMISTRY}: custom' needs '{CONF_CELL_VOLTAGE}' to say what the "
            f"cell charges to"
        )
    if chemistry is not None and chemistry != "custom" and CONF_CELL_VOLTAGE in config:
        raise cv.Invalid(
            f"'{CONF_CELL_VOLTAGE}' only applies to '{CONF_CHEMISTRY}: custom'; "
            f"'{chemistry}' already defines it"
        )
    if chemistry is not None and CONF_CHARGE_VOLTAGE in config:
        raise cv.Invalid(
            f"'{CONF_CHARGE_VOLTAGE}' exposes the charge voltage limit as an adjustable "
            f"control, which would let it be moved away from the value '{CONF_CHEMISTRY}' "
            f"sets. Use one or the other."
        )
    if chemistry is None and CONF_CHARGE_VOLTAGE not in config:
        raise cv.Invalid(
            f"Set '{CONF_CHEMISTRY}' so the charge voltage limit matches the cell. The "
            f"charger powers up at the PROG pin's default, which for a 1s pack is 4.2 V "
            f"and would damage a LiFePO4 cell."
        )
    return config


def _validate_ship_fet(config):
    """The IC locks SDRV_CTRL at 00 unless SFET_PRESENT is set, so these
    buttons would be controls that quietly do nothing."""
    if config[CONF_SHIP_FET_PRESENT]:
        return config
    present = [
        key
        for key in (CONF_SHIP_MODE, CONF_SHUTDOWN, CONF_POWER_CYCLE)
        if key in config
    ]
    if present:
        raise cv.Invalid(
            f"{', '.join(present)} drive SDRV_CTRL, which the charger locks at 00 unless "
            f"'{CONF_SHIP_FET_PRESENT}' is true. Set it if a ship FET is populated on SDRV, "
            f"or remove these controls."
        )
    return config


def _validate_ts_network(config):
    """The battery temperature is derived from the TS divider, so the divider
    has to be described before the sensor can mean anything."""
    if CONF_BATTERY_TEMPERATURE in config:
        missing = [
            key
            for key in (CONF_TS_RESISTOR_UPPER, CONF_TS_RESISTOR_LOWER)
            if key not in config
        ]
        if missing:
            raise cv.Invalid(
                f"'{CONF_BATTERY_TEMPERATURE}' needs {' and '.join(missing)} so the "
                f"TS pin reading can be converted to a temperature"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BQ25798Component),
            # Used to derive the charge-termination current as C/10.
            cv.Optional(CONF_BATTERY_CAPACITY): cv.int_range(min=1, max=20000),
            # Hard ceiling on every charge-current write. Set this to match
            # the board's copper rating, not the IC's 5 A capability.
            cv.Optional(CONF_MAX_CHARGE_CURRENT, default=5000): cv.int_range(
                min=50, max=5000
            ),
            # Ship / shutdown / power-cycle are locked out by the IC unless an
            # external ship FET is populated on SDRV.
            # The cell chemistry, from which the charge voltage limit is
            # derived and written at boot (and after any watchdog or register
            # reset). The PROG pin powers a 1s pack up at 4.2 V, which would
            # damage a LiFePO4 cell, so declaring the chemistry is how that
            # limit gets set -- not a slider someone has to remember.
            cv.Optional(CONF_CHEMISTRY): cv.one_of(
                *CHEMISTRY_CELL_VOLTAGE, lower=True
            ),
            cv.Optional(CONF_CELL_COUNT, default=1): cv.int_range(min=1, max=4),
            # Per-cell charge voltage, for chemistry: custom only.
            cv.Optional(CONF_CELL_VOLTAGE): cv.float_range(min=3.0, max=4.7),
            cv.Optional(CONF_DEFAULT_CHARGE_CURRENT): cv.int_range(min=50, max=5000),
            cv.Optional(CONF_SHIP_FET_PRESENT, default=False): cv.boolean,
            # The charger waits ~10 s before acting on SDRV_CTRL by default.
            cv.Optional(CONF_SHIP_FET_ACTION_DELAY, default=True): cv.boolean,
            # EN_BATOC. Fixed ~9.3 A threshold, so this is a short-circuit
            # backstop rather than copper protection.
            cv.Optional(CONF_BATTERY_OCP, default=False): cv.boolean,
            cv.Optional(CONF_WATCHDOG, default="disabled"): cv.one_of(
                *WATCHDOG_OPTIONS, lower=True
            ),
            cv.Optional(CONF_TS_RESISTOR_UPPER): cv.resistance,
            cv.Optional(CONF_TS_RESISTOR_LOWER): cv.resistance,
            cv.Optional(CONF_TS_NOMINAL_RESISTANCE, default="10kOhm"): cv.resistance,
            cv.Optional(CONF_TS_BETA, default=3435.0): cv.float_,
            # --- Sensors ---
            cv.Optional(CONF_VBUS_VOLTAGE): _voltage_sensor(),
            cv.Optional(CONF_VAC1_VOLTAGE): _voltage_sensor(),
            cv.Optional(CONF_VAC2_VOLTAGE): _voltage_sensor(),
            cv.Optional(CONF_VBAT_VOLTAGE): _voltage_sensor(),
            cv.Optional(CONF_VSYS_VOLTAGE): _voltage_sensor(),
            cv.Optional(CONF_IBUS_CURRENT): _current_sensor(),
            cv.Optional(CONF_IBAT_CURRENT): _current_sensor(),
            cv.Optional(CONF_BATTERY_TEMPERATURE): _temperature_sensor(),
            cv.Optional(CONF_DIE_TEMPERATURE): _temperature_sensor(),
            # --- Binary sensors ---
            cv.Optional(CONF_VBUS_PRESENT): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_POWER
            ),
            cv.Optional(CONF_VAC1_PRESENT): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_POWER
            ),
            cv.Optional(CONF_VAC2_PRESENT): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_POWER
            ),
            cv.Optional(CONF_POWER_GOOD): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_POWER
            ),
            cv.Optional(CONF_CHARGING): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_BATTERY_CHARGING
            ),
            cv.Optional(CONF_BATTERY_PRESENT): binary_sensor.binary_sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_THERMAL_REGULATION): binary_sensor.binary_sensor_schema(
                device_class="heat",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # --- Text sensors ---
            cv.Optional(CONF_CHARGE_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:battery-charging",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_INPUT_SOURCE): text_sensor.text_sensor_schema(
                icon="mdi:power-plug",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_CHARGER_FAULT): text_sensor.text_sensor_schema(
                icon="mdi:alert-circle-outline",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # --- Switches ---
            cv.Optional(CONF_ENABLE_CHARGING): switch.switch_schema(
                BQ25798ChargingSwitch,
                icon=ICON_FLASH,
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENABLE_HIZ): switch.switch_schema(
                BQ25798HizSwitch,
                icon="mdi:power-plug-off",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENABLE_MPPT): switch.switch_schema(
                BQ25798MpptSwitch,
                icon="mdi:solar-power",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENABLE_STAT_LED): switch.switch_schema(
                BQ25798StatLedSwitch,
                icon="mdi:led-on",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENABLE_ACDRV1): switch.switch_schema(
                BQ25798Acdrv1Switch,
                icon="mdi:electric-switch",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENABLE_ACDRV2): switch.switch_schema(
                BQ25798Acdrv2Switch,
                icon="mdi:electric-switch",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ENABLE_ICO): switch.switch_schema(
                BQ25798IcoSwitch,
                icon="mdi:tune",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            # --- Numbers ---
            cv.Optional(CONF_CHARGE_CURRENT): _number(
                BQ25798ChargeCurrentNumber, UNIT_MILLIAMPERE, 50, 5000, 10
            ),
            cv.Optional(CONF_CHARGE_VOLTAGE): _number(
                BQ25798ChargeVoltageNumber, UNIT_VOLT, 3.0, 18.8, 0.01
            ),
            cv.Optional(CONF_INPUT_CURRENT_LIMIT): _number(
                BQ25798InputCurrentNumber, UNIT_MILLIAMPERE, 100, 3300, 10
            ),
            cv.Optional(CONF_VINDPM): _number(
                BQ25798VindpmNumber, UNIT_VOLT, 3.6, 22.0, 0.1
            ),
            cv.Optional(CONF_MIN_SYSTEM_VOLTAGE): _number(
                BQ25798MinSysVoltageNumber, UNIT_VOLT, 2.5, 16.0, 0.25
            ),
            # --- Buttons ---
            cv.Optional(CONF_SHIP_MODE): button.button_schema(
                BQ25798ShipModeButton,
                icon="mdi:power-sleep",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_SHUTDOWN): button.button_schema(
                BQ25798ShutdownButton,
                icon="mdi:power-off",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_POWER_CYCLE): button.button_schema(
                BQ25798PowerCycleButton,
                icon="mdi:restart",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_RESET_REGISTERS): button.button_schema(
                BQ25798ResetButton,
                icon="mdi:backup-restore",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            # --- Selects (MPPT tuning) ---
            cv.Optional(CONF_MPPT_VOC_RATIO): select.select_schema(
                BQ25798VocRatioSelect,
                icon="mdi:percent",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_MPPT_VOC_DELAY): select.select_schema(
                BQ25798VocDelaySelect,
                icon="mdi:timer-sand",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_MPPT_VOC_RATE): select.select_schema(
                BQ25798VocRateSelect,
                icon="mdi:timer-refresh",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x6B)),
    _validate_chemistry,
    _validate_ship_fet,
    _validate_ts_network,
)

SENSOR_SETTERS = {
    CONF_VBUS_VOLTAGE: "set_vbus_voltage_sensor",
    CONF_VAC1_VOLTAGE: "set_vac1_voltage_sensor",
    CONF_VAC2_VOLTAGE: "set_vac2_voltage_sensor",
    CONF_VBAT_VOLTAGE: "set_vbat_voltage_sensor",
    CONF_VSYS_VOLTAGE: "set_vsys_voltage_sensor",
    CONF_IBUS_CURRENT: "set_ibus_current_sensor",
    CONF_IBAT_CURRENT: "set_ibat_current_sensor",
    CONF_BATTERY_TEMPERATURE: "set_battery_temperature_sensor",
    CONF_DIE_TEMPERATURE: "set_die_temperature_sensor",
}

BINARY_SENSOR_SETTERS = {
    CONF_VBUS_PRESENT: "set_vbus_present_sensor",
    CONF_VAC1_PRESENT: "set_vac1_present_sensor",
    CONF_VAC2_PRESENT: "set_vac2_present_sensor",
    CONF_POWER_GOOD: "set_power_good_sensor",
    CONF_CHARGING: "set_charging_sensor",
    CONF_BATTERY_PRESENT: "set_battery_present_sensor",
    CONF_THERMAL_REGULATION: "set_thermal_regulation_sensor",
}

TEXT_SENSOR_SETTERS = {
    CONF_CHARGE_STATUS: "set_charge_status_text_sensor",
    CONF_INPUT_SOURCE: "set_input_source_text_sensor",
    CONF_CHARGER_FAULT: "set_charger_fault_text_sensor",
}

SWITCH_SETTERS = {
    CONF_ENABLE_CHARGING: "set_charging_switch",
    CONF_ENABLE_HIZ: "set_hiz_switch",
    CONF_ENABLE_MPPT: "set_mppt_switch",
    CONF_ENABLE_STAT_LED: "set_stat_led_switch",
    CONF_ENABLE_ACDRV1: "set_acdrv1_switch",
    CONF_ENABLE_ACDRV2: "set_acdrv2_switch",
    CONF_ENABLE_ICO: "set_ico_switch",
}

NUMBER_SETTERS = {
    CONF_CHARGE_CURRENT: "set_charge_current_number",
    CONF_CHARGE_VOLTAGE: "set_charge_voltage_number",
    CONF_INPUT_CURRENT_LIMIT: "set_input_current_number",
    CONF_VINDPM: "set_vindpm_number",
    CONF_MIN_SYSTEM_VOLTAGE: "set_min_sys_voltage_number",
}

BUTTON_KEYS = [CONF_SHIP_MODE, CONF_SHUTDOWN, CONF_POWER_CYCLE, CONF_RESET_REGISTERS]

SELECT_SETTERS = {
    CONF_MPPT_VOC_RATIO: ("set_voc_ratio_select", VOC_RATIO_OPTIONS),
    CONF_MPPT_VOC_DELAY: ("set_voc_delay_select", VOC_DELAY_OPTIONS),
    CONF_MPPT_VOC_RATE: ("set_voc_rate_select", VOC_RATE_OPTIONS),
}


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    if CONF_BATTERY_CAPACITY in config:
        cg.add(var.set_battery_capacity(config[CONF_BATTERY_CAPACITY]))
    cg.add(var.set_max_charge_current(config[CONF_MAX_CHARGE_CURRENT]))
    cg.add(var.set_ship_fet_present(config[CONF_SHIP_FET_PRESENT]))
    cg.add(var.set_ship_fet_action_delay(config[CONF_SHIP_FET_ACTION_DELAY]))
    cg.add(var.set_battery_ocp(config[CONF_BATTERY_OCP]))
    if CONF_CHEMISTRY in config:
        per_cell = CHEMISTRY_CELL_VOLTAGE[config[CONF_CHEMISTRY]]
        if per_cell is None:
            per_cell = config[CONF_CELL_VOLTAGE]
        cg.add(var.set_charge_voltage_limit(per_cell * config[CONF_CELL_COUNT]))
    if CONF_DEFAULT_CHARGE_CURRENT in config:
        cg.add(var.set_default_charge_current(config[CONF_DEFAULT_CHARGE_CURRENT]))
    cg.add(var.set_watchdog(WATCHDOG_OPTIONS[config[CONF_WATCHDOG]]))

    if CONF_TS_RESISTOR_UPPER in config:
        cg.add(
            var.set_ts_network(
                config[CONF_TS_RESISTOR_UPPER],
                config[CONF_TS_RESISTOR_LOWER],
                config[CONF_TS_NOMINAL_RESISTANCE],
                config[CONF_TS_BETA],
            )
        )

    for key, setter in SENSOR_SETTERS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in BINARY_SENSOR_SETTERS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in TEXT_SENSOR_SETTERS.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in SWITCH_SETTERS.items():
        if key in config:
            sw = await switch.new_switch(config[key])
            await cg.register_component(sw, config[key])
            cg.add(sw.set_parent(var))
            cg.add(getattr(var, setter)(sw))

    for key, setter in NUMBER_SETTERS.items():
        if key in config:
            conf = config[key]
            num = await number.new_number(
                conf,
                min_value=conf["min_value"],
                max_value=conf["max_value"],
                step=conf["step"],
            )
            await cg.register_component(num, conf)
            cg.add(num.set_parent(var))
            cg.add(getattr(var, setter)(num))

    for key in BUTTON_KEYS:
        if key in config:
            btn = await button.new_button(config[key])
            await cg.register_component(btn, config[key])
            cg.add(btn.set_parent(var))

    for key, (setter, options) in SELECT_SETTERS.items():
        if key in config:
            sel = await select.new_select(config[key], options=options)
            await cg.register_component(sel, config[key])
            cg.add(sel.set_parent(var))
            cg.add(getattr(var, setter)(sel))

//
// Simple test for the AP_InertialSensor driver.
//

#include <AP_Baro/AP_Baro.h>
#include <AP_HAL/AP_HAL.h>
#include <AP_BoardConfig/AP_BoardConfig.h>
#include <AP_InertialSensor/AP_InertialSensor.h>
#include <AP_Compass/AP_Compass.h>
#include "UAVCAN_handler.h"
#include <GCS_MAVLink/GCS_Dummy.h>
#include <AP_HAL_ChibiOS/hwdef/common/watchdog.h>
#include <AP_Param/AP_Param.h>
#include <AP_Logger/AP_Logger.h>
#include "Parameters.h"
#include <AP_GPS/AP_GPS.h>
#include <AP_AHRS/AP_AHRS.h>

static Parameters g;

const AP_HAL::HAL &hal = AP_HAL::get_HAL();
int ownserial = -1;

void setup();
void loop();

#define UAVCAN_NODE_POOL_SIZE 8192
#ifdef UAVCAN_NODE_POOL_BLOCK_SIZE
#undef UAVCAN_NODE_POOL_BLOCK_SIZE
#endif
#define UAVCAN_NODE_POOL_BLOCK_SIZE 256
const struct LogStructure log_structure[] = {
    LOG_COMMON_STRUCTURES
};

// board specific config
static AP_BoardConfig BoardConfig;
static AP_InertialSensor ins;
static AP_Baro baro;
static Compass compass;
static AP_Int32 unused;
static AP_Logger logger{unused};
static AP_GPS gps;
static AP_AHRS_DCM ahrs;

void setup(void);
void loop(void);

static uint16_t _setup_sensor_health_mask = SENSOR_MASK;
static uint16_t _loop_sensor_health_mask = SENSOR_MASK;
static bool fault_recorded;

void setup(void)
{
    unused = -1;
    BoardConfig.init();
    // setup any board specific drivers
    hal.uartA->begin(AP_SERIALMANAGER_CONSOLE_BAUD, 32, 128);
    hal.uartB->begin(115200, 32, 128);
    hal.uartC->begin(9600, 32, 128);

    ins.init(100);
    // initialize the barometer
    baro.init();
    baro.calibrate();
    compass.init();
    hal.scheduler->delay(2000);
    hal.console->printf("Starting UAVCAN\n");
    hal.uartC->printf("Testing firmware updated on 22/5/2020 1122\n");
    hal.uartC->printf("Starting UAVCAN\n");
    hal.gpio->pinMode(0, HAL_GPIO_OUTPUT);
    UAVCAN_handler::init();
    g.load_parameters();
    g.num_cycles.set_and_save(g.num_cycles.get()+1);
    logger.Init(log_structure, ARRAY_SIZE(log_structure));

    //setup test
    hal.scheduler->delay(3000);
    AP::ins().update();
    AP::baro().update();
    AP::compass().read();
    for (uint8_t i = 0; i < 3; i++) {
        if (!AP::ins().get_accel_health(i)) {
            _setup_sensor_health_mask &= ~((1 << com::hex::equipment::jig::Status::ACCEL_HEALTH_OFF) << i);
        }
        if (!AP::ins().get_gyro_health(i)) {
            _setup_sensor_health_mask &= ~((1 << com::hex::equipment::jig::Status::GYRO_HEALTH_OFF) << i);
        }
    }
    for (uint8_t i = 0; i < 2; i++) {
        if (!AP::baro().healthy(i)) {
            _setup_sensor_health_mask &= ~((1 << com::hex::equipment::jig::Status::BARO_HEALTH_OFF) << i);
        }
    }
    for (uint8_t i = 0; i < 2; i++) {
        if (!AP::compass().healthy(i)) {
            _setup_sensor_health_mask &= ~((1 << 8) << i);
        }
    }

    if (_setup_sensor_health_mask != SENSOR_MASK) {
        if (!fault_recorded) {
            fault_recorded = true;
            g.num_fails.set_and_save(g.num_fails.get()+1);
            g.setup_sensor_health.set_and_save(g.setup_sensor_health.get()&_setup_sensor_health_mask);
        }
    }
}

#define IMU_HIGH_TEMP 70
// #define IMU_LOW_TEMP  55

static int8_t _heater_target_temp = IMU_HIGH_TEMP;
static uint32_t _hold_start_ms;
static uint8_t _heater_state;
static uint32_t _led_blink_ms;
static uint32_t _led_blink_state;

void loop()
{
    //loop test
    AP::ins().update();
    AP::baro().update();
    AP::compass().read();
    for (uint8_t i = 0; i < 3; i++) {
        if (!AP::ins().get_accel_health(i)) {
            _loop_sensor_health_mask &= ~((1 << com::hex::equipment::jig::Status::ACCEL_HEALTH_OFF) << i);
        }
        if (!AP::ins().get_gyro_health(i)) {
            _loop_sensor_health_mask &= ~((1 << com::hex::equipment::jig::Status::GYRO_HEALTH_OFF) << i);
        }
    }
    for (uint8_t i = 0; i < 2; i++) {
        if (!AP::baro().healthy(i)) {
            _loop_sensor_health_mask &= ~((1 << com::hex::equipment::jig::Status::BARO_HEALTH_OFF) << i);
        }
    }
    for (uint8_t i = 0; i < 2; i++) {
        if (!AP::compass().healthy(i)) {
            _loop_sensor_health_mask &= ~((1 << 8) << i);
        }
    }

    if (_loop_sensor_health_mask != SENSOR_MASK) {
        if (!fault_recorded) {
            fault_recorded = true;
            g.num_fails.set_and_save(g.num_fails.get()+1);
            g.loop_sensor_health.set_and_save(g.loop_sensor_health.get()&_loop_sensor_health_mask);
        }
    }

    // Do LED Patterns
    if ((AP_HAL::millis() - _led_blink_ms) > 2000) {
        _led_blink_state = 0;
        _led_blink_ms = AP_HAL::millis();
        hal.console->printf("SENSOR_MASK: 0x%x NUM_RUNS: %d NUM_FAILS: %d LOOP_TEST_FLAGS: 0x%x SETUP_TEST_FLAGS: 0x%x\n", SENSOR_MASK, g.num_cycles.get(), g.num_fails.get(), g.loop_sensor_health.get(), g.setup_sensor_health.get());
        hal.uartC->printf("SENSOR_MASK: 0x%x NUM_RUNS: %d NUM_FAILS: %d LOOP_TEST_FLAGS: 0x%x SETUP_TEST_FLAGS: 0x%x\n", SENSOR_MASK, g.num_cycles.get(), g.num_fails.get(), g.loop_sensor_health.get(), g.setup_sensor_health.get());
        //Write IMU Data to Log
        logger.Write_IMU();
    }

    if ((_led_blink_state < (g.num_fails.get()*2)) && 
        ((AP_HAL::millis() - _led_blink_ms) > (_led_blink_state*30))) {
        _led_blink_state++;
        hal.gpio->toggle(0);
    }


    if ((_heater_target_temp - AP::ins().get_temperature(0)) > 0.5f) {
        _heater_state = com::hex::equipment::jig::Status::HEATER_STATE_HEATING;
        _hold_start_ms = AP_HAL::millis();
    } else {
        _heater_state = com::hex::equipment::jig::Status::HEATER_STATE_HOLDING;
    }

    //hal.console->printf("Temp Delta: %d %d %f\n", _heater_state, AP_HAL::millis() - _hold_start_ms, (_heater_target_temp - AP::ins().get_temperature(0)));

    // if ((_heater_state == com::hex::equipment::jig::Status::HEATER_STATE_HOLDING) && ((AP_HAL::millis() - _hold_start_ms) >= 10000)) {
    //     if (_heater_target_temp == IMU_HIGH_TEMP) {
    //         _heater_target_temp = IMU_LOW_TEMP;
    //     } else if (_heater_target_temp == IMU_LOW_TEMP) {
    //         _heater_target_temp = IMU_HIGH_TEMP;
    //     }
    // }

    BoardConfig.set_target_temp(_heater_target_temp);
    if ((_setup_sensor_health_mask & _loop_sensor_health_mask) == SENSOR_MASK) {
        UAVCAN_handler::set_sensor_states(0x3FF, _heater_state);
    } else {
        UAVCAN_handler::set_sensor_states((_setup_sensor_health_mask & _loop_sensor_health_mask), _heater_state);
    }

    UAVCAN_handler::loop_all();

    /**
    // print console received bit
    if (hal.console->available() > 0) {
        buff[len_before_reboot] = hal.console->read();
        len_before_reboot++;
        hal.uartC->printf("Received bit: ");
        hal.console->printf("Received bit: ");
        hal.uartC->printf(buff);
        hal.console->printf(buff);
        hal.uartC->printf("\n");
        hal.console->printf("\n");
    }
    **/
    
    // auto-reboot for --upload
    if (hal.console->available() > 10) {
        hal.console->printf("rebooting\n");
        hal.uartC->printf("rebooting\n");
        while (hal.console->available()) {
            hal.console->read();
        }
        hal.scheduler->reboot(true);
    }
}

GCS_Dummy _gcs;

extern mavlink_system_t mavlink_system;

const AP_Param::GroupInfo GCS_MAVLINK_Parameters::var_info[] = {
    AP_GROUPEND
};

AP_HAL_MAIN();

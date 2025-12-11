#ifndef TELEMETRY_ENDPOINT_H
#define TELEMETRY_ENDPOINT_H

#include "CRSFEndpoint.h"
#include "crsf_protocol.h"
#include "telemetry_main.h"

class TelemetryEndpoint : public CRSFEndpoint {
public:
    TelemetryEndpoint();
    virtual ~TelemetryEndpoint() = default;

    void handleMessage(const crsf_header_t *message) override;
    bool handleRaw(const crsf_header_t *message) override { return false; }

private:
    void processBatterySensor(const crsf_sensor_battery_t* battery);
    void processAttitudeSensor(const crsf_sensor_attitude_t* attitude);
    void processBaroAltitude(const crsf_sensor_baro_vario_t* baro);
    void processLinkStatistics(const crsfLinkStatistics_t* linkStats);
};

#endif // TELEMETRY_ENDPOINT_H
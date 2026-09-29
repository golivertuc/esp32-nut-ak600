#ifndef KRAULER_DRIVER_H
#define KRAULER_DRIVER_H

#include "GenericDriver.h"
#include "UPSData.h"
#include <cstdlib>

/**
 * @brief TBCin AK-600 / Megatec Q1 ("Krauler") protocol driver.
 *
 * USB-serial bridge (chip "XJB UPS MON V2.0", VID 0x0001 / PID 0x0000) that
 * speaks the Megatec command set through HID string descriptors — same
 * approach as NUT's blazer_usb krauler subdriver:
 *
 *   Q1 -> string idx 3  -> "(in_v fault_v out_v load% freq batt_v temp <8 status bits>"
 *   I  -> string idx 12 -> "#company(15) model(10) version(10)"
 *   F  -> string idx 13 -> "#in_nom in_cur_nom batt_nom in_freq_nom"
 *
 * NUT reference response (blazer.c test vector):
 *   (215.0 195.0 230.0 014 49.0 2.27 30.0 00101000
 *
 * Status bits (blazer_status, MSB first):
 *   [0] utility fail (OB)  [1] battery low (LB)  [2] bypass/boost active
 *   [3] selftest failed    [4] standby type      [5] test in progress
 *   [6] shutdown active    [7] beeper on
 *
 * The XJB also emits spontaneous 2-byte INPUT reports; those are proprietary
 * status blips, NOT PDC values, so decodeReport() ignores them (NUT ignores
 * the HID reports of this chip class too — all values come from Q1/I/F).
 */
class KraulerDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "KraulerDriver"; }

    void setup() override {
        GenericDriver::setup();
        _batt_nom = 0.0f;
        // Stale keys from the HID-usage decode (this chip's report descriptor reuses
        // PDC usages for non-standard values): clear them so /api/ups-vars is clean.
        // They are re-set by the Q1/F parse below.
        UPSData* data = nullptr; // populated lazily on first loop(); see onLoop
        (void)data;
    }

    // Strings only: Q1 every quick cycle, I+F on full cycles. No report polling.
    void buildPollLists(IUSBHostUPS* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const override {
        (void)host;
        quick.clear();
        full.clear();
        quick.push_back(PollItem{STRING_ITEM, 3, 0});   // Q1
        full.push_back(PollItem{STRING_ITEM, 3, 0});    // Q1
        full.push_back(PollItem{STRING_ITEM, 12, 0});   // I  (info)
        full.push_back(PollItem{STRING_ITEM, 13, 0});   // F  (rating)
    }

    // Standard device strings are empty on this chip (index 3 IS the Q1 command),
    // so do not auto-request iManufacturer/iProduct/iSerial.
    void collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const override {
        (void)host; (void)data; (void)out;
    }

    // Ignore the proprietary INPUT reports (see class comment).
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type,
                      const uint8_t* data, size_t length, UPSData& ups_data) override {
        (void)host; (void)report_id; (void)report_type; (void)data; (void)length; (void)ups_data;
    }

    void onLoop(IUSBHostUPS* host, UPSData& data) override {
        // One-shot cleanup of keys the old HID-usage mapping left behind.
        if (!_cleaned) {
            _cleaned = true;
            data.remove("input.transfer.low");
            data.remove("input.voltage.nominal");
            data.remove("ups.beeper.switchable");
        }
        (void)host;
    }

    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t* data, size_t length, UPSData& ups_data) override {
        if (length < 2 || data[1] != 0x03) {
            // Not a string descriptor payload: fall back to the base mapping.
            GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
            return;
        }
        String s = stringPayload(data, length);
        if (index == 3)  { parseQ1(s, ups_data);  return; }
        if (index == 12) { parseInfo(s, ups_data); return; }
        if (index == 13) { parseRating(s, ups_data); return; }
        GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
    }

private:
    static String stringPayload(const uint8_t* data, size_t length) {
        uint8_t str_len = data[0];
        String s = "";
        for (size_t i = 2; i < str_len && i < length; i += 2) {
            char c = (char)data[i]; // low byte of UTF-16LE
            if (c >= 0x20 && c < 0x7F) s += c;
        }
        s.trim();
        return s;
    }

    // Q1: "(226.0 195.0 226.0 014 49.0 13.6 30.0 00001000"
    // Fields (NUT blazer_status): in_v, fault_v, out_v, load%, freq, batt_v, temp, status bits.
    void parseQ1(const String& s, UPSData& d) {
        if (s.length() < 8 || s.charAt(0) != '(') return; // "UPS No Ack" etc.
        const char* p = s.c_str() + 1;
        char* end = NULL;
        double f[7];
        for (int i = 0; i < 7; i++) {
            f[i] = strtod(p, &end);
            if (end == p) return; // non-numeric -> not a Q1 reply
            p = end;
        }
        String bits = "";
        while (*p == ' ' || *p == '\t') p++;
        while (*p == '0' || *p == '1') { bits += (char)*p; p++; }
        if (bits.length() < 8) return;

        d.set("input.voltage", String(f[0], 1));
        d.set("input.voltage.fault", String(f[1], 1));
        d.set("output.voltage", String(f[2], 1));
        d.set("ups.load", String((int)f[3]));
        d.set("input.frequency", String(f[4], 1));

        // Battery voltage: Q1 may report per-cell or total; NUT blazer_packs picks the
        // multiplier that lands the product inside [0.8*nominal, 1.25*nominal].
        double nom = (_batt_nom > 0.5f) ? (double)_batt_nom : 12.0;
        double act = f[5];
        static const double mults[] = { 6.0, 4.0, 3.0, 2.0, 1.0 };
        for (size_t i = 0; i < sizeof(mults) / sizeof(mults[0]); i++) {
            double v = f[5] * mults[i];
            if (v <= 1.25 * nom && v >= 0.8 * nom) { act = v; break; }
        }
        d.set("battery.voltage", String(act, 2));

        // Charge estimate (NUT blazer_battery default curve: 10.4V..13.0V per 12V nominal)
        double low = 10.4 * nom / 12.0;
        double high = 13.0 * nom / 12.0;
        if (high > low) {
            double charge = 100.0 * (act - low) / (high - low);
            if (charge < 0) charge = 0;
            if (charge > 100) charge = 100;
            d.set("battery.charge", String((int)charge));
        }

        d.set("ups.temperature", String(f[6], 1));

        // Status bits (NUT blazer_status)
        bool utilFail = (bits.charAt(0) == '1');
        d.set("ups.status.ac_present", utilFail ? "0" : "1");
        d.set("ups.status.discharging", utilFail ? "1" : "0");
        d.set("ups.status.battery_low", bits.charAt(1) == '1' ? "1" : "0");
        d.set("ups.status.shutdown_imminent", bits.charAt(6) == '1' ? "1" : "0");
        d.set("ups.beeper.status", bits.charAt(7) == '1' ? "enabled" : "disabled");
        d.updateRealPower();
    }

    // I: "#-------------   ------     VT12046Q"  (company 15, model 10, version 10)
    void parseInfo(const String& s, UPSData& d) {
        if (s.length() < 36 || s.charAt(0) != '#') return;
        String c = s.substring(1, 16);
        String m = s.substring(16, 26);
        String v = s.substring(26, 36);
        c.trim(); m.trim(); v.trim();
        if (c.length() > 0) d.set("ups.mfr", c);
        if (m.length() > 0) d.set("ups.model", m);
        if (v.length() > 0) d.set("ups.firmware", v);
    }

    // F: "#230.0 000 024.0 50.0"  (in_v nominal, in current nominal, batt nominal, in freq nominal)
    void parseRating(const String& s, UPSData& d) {
        if (s.length() < 4 || s.charAt(0) != '#') return;
        const char* p = s.c_str() + 1;
        char* end = NULL;
        double f[4];
        for (int i = 0; i < 4; i++) {
            f[i] = strtod(p, &end);
            if (end == p) return;
            p = end;
        }
        if (f[0] > 0) d.set("input.voltage.nominal", String((int)f[0]));
        if (f[1] > 0) d.set("input.current.nominal", String(f[1], 1));
        if (f[2] > 0) {
            _batt_nom = (float)f[2];
            d.set("battery.voltage.nominal", String(f[2], 1));
        }
        if (f[3] > 0) d.set("input.frequency.nominal", String((int)f[3]));
        d.updateRealPower();
    }

    float _batt_nom = 0.0f;
    bool _cleaned = false;
};

#endif // KRAULER_DRIVER_H

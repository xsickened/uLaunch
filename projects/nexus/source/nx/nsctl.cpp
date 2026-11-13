// Lesen von Titelname und Icon (NACP + Icon) passend zur Firmware.
// Logik übernommen aus uLaunch 1.2.3 / libnx-ext (ns-ext.c, GPLv2): ab 21.0.0 neuer Befehl 19 über
// IReadOnlyApplicationControlDataInterface, 19.x–20.x GetApplicationControlData2, davor der klassische Befehl.
// Eigener Funktionsname, damit es keine Namenskonflikte mit neueren libnx-Versionen gibt.
#include "nsctl.hpp"

namespace nexus {

    Result GetControlData(u64 tid, NsApplicationControlData *buf, u64 *actual) {
        if(hosversionAtLeast(21, 0, 0)) {
            Service srv = {};
            Result rc = nsGetReadOnlyApplicationControlDataInterface(&srv);
            if(R_FAILED(rc)) return rc;
            const struct {
                u8 source;
                u8 flags[2];
                u8 pad[5];
                u64 application_id;
            } in = { static_cast<u8>(NsApplicationControlSource_Storage), { 0xFF, 0 }, { 0 }, tid };
            u32 tmp[3] = {};
            rc = serviceDispatchInOut(&srv, 19, in, tmp,
                .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_Out },
                .buffers = { { buf, sizeof(NsApplicationControlData) } },
            );
            if(R_SUCCEEDED(rc) && actual) *actual = tmp[2];
            serviceClose(&srv);
            return rc;
        }
        if(hosversionAtLeast(19, 0, 0)) {
            return nsGetApplicationControlData2(NsApplicationControlSource_Storage, tid, buf, sizeof(NsApplicationControlData), 0xFF, 0, actual, nullptr);
        }
        return nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, buf, sizeof(NsApplicationControlData), actual);
    }

}

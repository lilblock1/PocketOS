#include <assert.h>
#include "pocketos-radio-idle.h"
int main(void){
    struct po_radio_observation s={
        .wifi_link=PO_LINK_IDLE,.bt_clients=PO_LINK_IDLE,
        .wifi_enabled=1,.bt_enabled=1,
        .wifi_idle_since_ms=100,.bt_idle_since_ms=100
    };
    assert(po_radio_idle_advice(s,300099)==PO_RADIO_KEEP);
    assert(po_radio_idle_advice(s,300100)==(PO_RADIO_SLEEP_WIFI|PO_RADIO_SLEEP_BT));
    assert(po_radio_idle_advice(s,-1)==PO_RADIO_KEEP);
    s.wifi_link=PO_LINK_UNKNOWN;
    assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);
    s.wifi_link=PO_LINK_ACTIVE;s.bt_clients=PO_LINK_ACTIVE;
    assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);
    s.wifi_link=PO_LINK_IDLE;s.bt_clients=PO_LINK_IDLE;
    s.update_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.update_busy=0;
    s.transfer_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.transfer_busy=0;
    s.radio_job_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.radio_job_busy=0;
    s.external_radio_lease=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.external_radio_lease=0;
    s.discovery_busy=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_KEEP);s.discovery_busy=0;
    s.audio_bt_playing=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);s.audio_bt_playing=0;
    s.wifi_page=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);s.wifi_page=0;
    s.bt_page=1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);s.bt_page=0;
    s.wifi_enabled=0;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);s.wifi_enabled=1;
    s.bt_enabled=0;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);s.bt_enabled=1;
    s.wifi_idle_since_ms=-1;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_BT);s.wifi_idle_since_ms=100;
    s.bt_clients=PO_LINK_UNKNOWN;assert(po_radio_idle_advice(s,900000)==PO_RADIO_SLEEP_WIFI);
    /* Consecutive-idle state machine: a status change must reset the clock. */
    struct po_radio_tracker tracker;
    po_radio_tracker_reset(&tracker);
    s.bt_clients=PO_LINK_IDLE;s.wifi_link=PO_LINK_IDLE;
    s.wifi_enabled=s.bt_enabled=1;
    s.wifi_idle_since_ms=s.bt_idle_since_ms=-1;
    po_radio_tracker_update(&tracker,&s,1000);
    assert(s.wifi_idle_since_ms==1000&&s.bt_idle_since_ms==1000);
    assert(po_radio_idle_advice(s,300999)==PO_RADIO_KEEP);
    assert(po_radio_idle_advice(s,301000)==(PO_RADIO_SLEEP_WIFI|PO_RADIO_SLEEP_BT));
    s.update_busy=1;
    po_radio_tracker_update(&tracker,&s,301001);
    assert(s.wifi_idle_since_ms==-1&&s.bt_idle_since_ms==-1);
    s.update_busy=0;s.bt_clients=PO_LINK_ACTIVE;
    po_radio_tracker_update(&tracker,&s,302000);
    assert(s.wifi_idle_since_ms==302000&&s.bt_idle_since_ms==-1);
    s.bt_clients=PO_LINK_UNKNOWN;
    po_radio_tracker_update(&tracker,&s,605000);
    assert(po_radio_idle_advice(s,605000)==PO_RADIO_SLEEP_WIFI);
    s.wifi_link=PO_LINK_UNKNOWN;s.bt_clients=PO_LINK_IDLE;
    po_radio_tracker_update(&tracker,&s,605001);
    assert(s.wifi_idle_since_ms==-1&&s.bt_idle_since_ms==605001);
    po_radio_tracker_update(&tracker,&s,605000); /* clock rollback */
    assert(s.wifi_idle_since_ms==-1&&s.bt_idle_since_ms==-1);
    po_radio_tracker_update(&tracker,&s,605002);
    assert(s.bt_idle_since_ms==605002);
    s.audio_bt_playing=1;
    po_radio_tracker_update(&tracker,&s,905002);
    assert(s.bt_idle_since_ms==-1&&po_radio_idle_advice(s,905002)==PO_RADIO_KEEP);
    s.audio_bt_playing=0;
    po_radio_tracker_update(&tracker,&s,905003);
    assert(s.bt_idle_since_ms==905003);
    return 0;
}

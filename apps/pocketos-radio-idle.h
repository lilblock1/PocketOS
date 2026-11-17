/* PocketOS Phase 6 radio sleep policy.
 * Separate physical interface presence from service readiness and active links.
 * Fail closed: unknown observations never authorize power changes. */
#ifndef POCKETOS_RADIO_IDLE_H
#define POCKETOS_RADIO_IDLE_H
enum po_link { PO_LINK_UNKNOWN=-1, PO_LINK_IDLE=0, PO_LINK_ACTIVE=1 };
enum po_radio_action { PO_RADIO_KEEP=0, PO_RADIO_SLEEP_WIFI=1, PO_RADIO_SLEEP_BT=2 };
#define PO_RADIO_IDLE_MS 300000LL
struct po_radio_observation {
    enum po_link wifi_link; /* verified wpa_supplicant association state */
    enum po_link bt_clients; /* verified Bluetooth connections, not adapter presence */
    int wifi_enabled, bt_enabled;
    int update_busy, transfer_busy, radio_job_busy, audio_bt_playing;
    int wifi_page, bt_page;
    int discovery_busy, external_radio_lease;
    long long wifi_idle_since_ms, bt_idle_since_ms;
};
static inline int po_radio_old_enough(long long now,long long since) {
    return now>=0&&since>=0&&now>=since&&(now-since)>=PO_RADIO_IDLE_MS;
}
/* An idle radio is only a candidate; an independently authorized hardware
 * adapter must confirm its owner and current state before switching power. */
static inline unsigned po_radio_idle_advice(struct po_radio_observation s,long long now) {
    if(s.update_busy||s.transfer_busy||s.radio_job_busy||s.discovery_busy||
       s.external_radio_lease)return PO_RADIO_KEEP;
    unsigned actions=PO_RADIO_KEEP;
    if(s.wifi_enabled&&s.wifi_link==PO_LINK_IDLE&&!s.wifi_page&&
       po_radio_old_enough(now,s.wifi_idle_since_ms))
        actions|=PO_RADIO_SLEEP_WIFI;
    if(s.bt_enabled&&s.bt_clients==PO_LINK_IDLE&&!s.audio_bt_playing&&!s.bt_page&&
       po_radio_old_enough(now,s.bt_idle_since_ms))
        actions|=PO_RADIO_SLEEP_BT;
    return actions;
}
#endif

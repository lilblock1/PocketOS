/* Phase 6: pure output policy. Hardware discovery must supply verified states.
 * A Bluetooth control connection alone is NOT an available audio sink. */
#ifndef POCKETOS_AUDIO_ROUTE_H
#define POCKETOS_AUDIO_ROUTE_H
enum po_route { PO_ROUTE_NONE, PO_ROUTE_WIRED, PO_ROUTE_BT };
enum po_presence { PO_UNKNOWN, PO_ABSENT, PO_READY };
struct po_audio_state {
    enum po_presence wired, bluetooth;
    int wired_volume, bluetooth_volume;
};
static inline enum po_route po_choose_route(struct po_audio_state s) {
    if(s.wired==PO_READY) return PO_ROUTE_WIRED;
    if(s.bluetooth==PO_READY) return PO_ROUTE_BT;
    return PO_ROUTE_NONE;
}
static inline int po_safe_volume(int v) {
    return v<0?0:v>50?50:v;
}
static inline int po_route_volume(struct po_audio_state s, enum po_route r) {
    if(r==PO_ROUTE_WIRED) return po_safe_volume(s.wired_volume);
    if(r==PO_ROUTE_BT) return po_safe_volume(s.bluetooth_volume);
    return 0;
}
#endif

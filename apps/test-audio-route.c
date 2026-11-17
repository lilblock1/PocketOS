#include <assert.h>
#include "pocketos-audio-route.h"
int main(void) {
    struct po_audio_state s={.wired_volume=22,.bluetooth_volume=38};
    for(int w=PO_UNKNOWN;w<=PO_READY;w++)
        for(int b=PO_UNKNOWN;b<=PO_READY;b++){
            s.wired=(enum po_presence)w;s.bluetooth=(enum po_presence)b;
            enum po_route expected=w==PO_READY?PO_ROUTE_WIRED:b==PO_READY?PO_ROUTE_BT:PO_ROUTE_NONE;
            assert(po_choose_route(s)==expected);
            assert(po_route_volume(s,expected)==(expected==PO_ROUTE_WIRED?22:expected==PO_ROUTE_BT?38:0));
        }
    s.wired_volume=100;s.bluetooth_volume=-1;
    assert(po_route_volume(s,PO_ROUTE_WIRED)==50);
    assert(po_route_volume(s,PO_ROUTE_BT)==0);
    return 0;
}

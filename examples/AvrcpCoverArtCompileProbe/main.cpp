#include "AvrcpBrowsing.h"
#include "AvrcpCoverArt.h"
#include "MediaAttributeIterator.h"
volatile bool initialize_for_link_check = false;
static bool consume(const picow::avrcp::AvrcpCoverArt::Data &, void *) {
  return true;
}
static void observe(const picow::avrcp::AvrcpCoverArt::Event &, void *) {}
int main() {
  static picow::avrcp::AvrcpController control;
  static picow::avrcp::AvrcpTarget target;
  static picow::avrcp::AvrcpCoverArt art;
  static picow::avrcp::AvrcpBrowsing browsing;
  if (initialize_for_link_check) {
    picow::avrcp::AvrcpController::Config c;
    c.peer_count = 1;
    c.peers[0] = {{1, 2, 3, 4, 5, 6}};
    target.setSupported(0x7e, true);
    target.setPassThroughHandler([](const picow::avrcp::AvrcpTarget::PassThroughRequest &, void *) {
      return picow::avrcp::AvrcpTarget::PassThroughDecision::Rejected;
    }, nullptr);
    target.begin(c, true);
    control.begin(c, true);
    browsing.begin(c, true);
    control.setCommandTimeout(15000);
    control.connect(c.peers[0]);
    control.poll(1000);
    control.cancelConnection(c.peers[0]);
    (void)control.cancellationPending(c.peers[0]);
    (void)control.cancellationStatus(c.peers[0]);
    target.cancelConnection(c.peers[0]);
    (void)target.cancellationPending(c.peers[0]);
    (void)target.cancellationStatus(c.peers[0]);
    const uint8_t vendor_payload[] = {1,2,3};
    control.customCommand(control.bindingForPeer(c.peers[0]), 0, 9, 0,
                          0x77, 0x123456, vendor_payload, sizeof(vendor_payload));
    control.setCustomCallback(nullptr);
    browsing.setTimeouts(15000,15000,15000,15000);
    browsing.poll(1000);
    browsing.connect(c.peers[0]);
    art.begin(c);
    art.setDataHandler(consume);
    art.setCallback(observe);
    art.setTimeouts(15000, 60000);
    art.connect(c.peers[0]);
    auto b = art.bindingForPeer(c.peers[0]);
    art.thumbnail(b, "1234567");
    art.image(b, "1234567", nullptr);
    art.image(b, "1234567", "<image-descriptor version=\"1.0\"/>");
    art.properties(b, "1234567");
    art.poll();
    art.poll(1000);
    const uint8_t attribute[] = {0,0,0,1,0,106,0,0};
    picow::avrcp::MediaAttributeIterator iterator(attribute, sizeof(attribute));
    picow::avrcp::MediaAttributeIterator::Attribute value;
    if (iterator.next(value) != picow::avrcp::MediaAttributeIterator::Result::Item) return 1;
    art.disconnect(b);
    art.cancel(b);
    art.cancelPeer(c.peers[0]);
    art.stop();
    browsing.stop();
    control.stop();
    target.stop();
  }
  return 0;
}

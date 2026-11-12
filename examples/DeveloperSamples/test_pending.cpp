#include "PendingMessage.h"
#include <cassert>
#include <cstdio>
#include <string>
int main() {
  spp::State state;
  state.routes[0].generation = 1;
  state.routes[0].phase = spp::Phase::Open;
  spp::Client client;
  PendingMessage message{spp::handleOf(state, 0), 0, true};
  client.next = {0, spp::Error::Busy};
  message.poll(client, state, 0);
  message.poll(client, state, 0);
  assert(message.active && message.offset == 0 && client.accepted.empty());
  client.next = {3, spp::Error::None};
  message.poll(client, state, 0);
  assert(message.active && message.offset == 3);
  client.next = {0, spp::Error::Busy};
  message.poll(client, state, 0);
  assert(message.active && message.offset == 3 && client.accepted.size() == 3);
  client.next = {0, spp::Error::WouldBlock};
  message.poll(client, state, 0);
  assert(message.active && message.offset == 3 && client.accepted.size() == 3);
  client.next = {100, spp::Error::None};
  message.poll(client, state, 0);
  assert(!message.active);
  assert(std::string(client.accepted.begin(), client.accepted.end()) == PendingMessage::text);
  message = {spp::handleOf(state, 0), 0, true};
  client.next = {0, spp::Error::Busy};
  message.poll(client, state, 0);
  ++state.routes[0].generation;
  const auto size = client.accepted.size();
  message.poll(client, state, 0);
  assert(!message.active && client.accepted.size() == size);
  message = {spp::handleOf(state, 0), 0, true};
  client.next = {0, spp::Error::Busy};
  message.poll(client, state, 0);
  state.routes[0].phase = spp::Phase::Closed;
  message.poll(client, state, 0);
  assert(!message.active && client.accepted.size() == size);
  state.routes[0].phase = spp::Phase::Open;
  message = {spp::handleOf(state, 0), 0, true};
  client.next = {3, spp::Error::None};
  message.poll(client, state, 0);
  const auto fatal_size = client.accepted.size();
  client.next = {0, spp::Error::Failed};
  message.poll(client, state, 0);
  assert(!message.active);
  client.next = {100, spp::Error::None};
  message.poll(client, state, 0);
  assert(client.accepted.size() == fatal_size);
  puts("PASS: partial writes, Busy/WouldBlock, no duplicate bytes, stale/closed handles, fatal error");
}

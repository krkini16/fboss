/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;
using std::make_pair;
using std::make_shared;
using std::shared_ptr;

TEST(Acl, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto aclEntry = make_shared<AclEntry>(AclEntryID(0));
  stateV0->addAcl(aclEntry);
  auto aclV0 = stateV0->getAcl(AclEntryID(0));
  EXPECT_EQ(0, aclV0->getGeneration());
  EXPECT_FALSE(aclV0->isPublished());
  EXPECT_EQ(AclEntryID(0), aclV0->getID());

  aclV0->publish();
  EXPECT_TRUE(aclV0->isPublished());

  cfg::SwitchConfig config;
  config.acls.resize(1);
  config.acls[0].id = 100;
  config.acls[0].action = cfg::AclAction::DENY;
  config.acls[0].__isset.srcIp = true;
  config.acls[0].__isset.dstIp = true;
  config.acls[0].srcIp = "192.168.0.1";
  config.acls[0].dstIp = "192.168.0.0/24";
  config.acls[0].__isset.srcPort = true;
  config.acls[0].srcPort = 5;
  config.acls[0].__isset.dstPort = true;
  config.acls[0].dstPort = 8;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl(AclEntryID(100));
  ASSERT_NE(nullptr, aclV1);
  EXPECT_NE(aclV0, aclV1);

  EXPECT_EQ(AclEntryID(100), aclV1->getID());
  EXPECT_EQ(cfg::AclAction::DENY, aclV1->getAction());
  EXPECT_EQ(5, aclV1->getSrcPort());
  EXPECT_EQ(8, aclV1->getDstPort());
  EXPECT_FALSE(aclV1->isPublished());

  config.acls[0].dstIp = "invalid address";
  EXPECT_THROW(publishAndApplyConfig(
    stateV1, &config, platform.get()), folly::IPAddressFormatException);

  config.acls[0].id = 200;
  config.acls[0].__isset.dstIp = false;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto aclV2 = stateV2->getAcl(AclEntryID(200));
  // We should handle the field removal correctly
  EXPECT_NE(nullptr, aclV2);
  EXPECT_FALSE(aclV2->getDstIp().first);

  // Non-existent entry
  auto acl2V2 = stateV2->getAcl(AclEntryID(100));
  ASSERT_EQ(nullptr, acl2V2);

  cfg::SwitchConfig configV1;
  configV1.acls.resize(1);
  configV1.acls[0].id = 101;
  configV1.acls[0].action = cfg::AclAction::PERMIT;

  // set ranges
  configV1.acls[0].srcL4PortRange.min = 1;
  configV1.acls[0].srcL4PortRange.max = 2;
  configV1.acls[0].__isset.srcL4PortRange = true;
  configV1.acls[0].dstL4PortRange.min = 3;
  configV1.acls[0].dstL4PortRange.max = 4;
  configV1.acls[0].__isset.dstL4PortRange = true;

  auto stateV3 = publishAndApplyConfig(stateV2, &configV1, platform.get());
  EXPECT_NE(nullptr, stateV3);
  auto aclV3 = stateV3->getAcl(AclEntryID(101));
  ASSERT_NE(nullptr, aclV3);
  EXPECT_NE(aclV0, aclV3);
  EXPECT_EQ(AclEntryID(101), aclV3->getID());
  EXPECT_EQ(cfg::AclAction::PERMIT, aclV3->getAction());
  EXPECT_FALSE(!aclV3->getSrcL4PortRange());
  EXPECT_EQ(aclV3->getSrcL4PortRange().value().getMin(), 1);
  EXPECT_EQ(aclV3->getSrcL4PortRange().value().getMax(), 2);
  EXPECT_FALSE(!aclV3->getDstL4PortRange());
  EXPECT_EQ(aclV3->getDstL4PortRange().value().getMin(), 3);
  EXPECT_EQ(aclV3->getDstL4PortRange().value().getMax(), 4);

  // test min > max case
  configV1.acls[0].srcL4PortRange.min = 3;
  EXPECT_THROW(publishAndApplyConfig(stateV3, &configV1, platform.get()),
    FbossError);
  // test max > 65535 case
  configV1.acls[0].srcL4PortRange.max = 65536;
  EXPECT_THROW(publishAndApplyConfig(stateV3, &configV1, platform.get()),
    FbossError);
  // set packet length rangeJson
  cfg::SwitchConfig configV2;
  configV2.acls.resize(1);
  configV2.acls[0].id = 101;
  configV2.acls[0].action = cfg::AclAction::PERMIT;

  // set pkt length range
  configV2.acls[0].pktLenRange.min = 34;
  configV2.acls[0].pktLenRange.max = 1500;
  configV2.acls[0].__isset.pktLenRange = true;

  auto stateV4 = publishAndApplyConfig(stateV3, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV4);
  auto aclV4 = stateV4->getAcl(AclEntryID(101));
  ASSERT_NE(nullptr, aclV4);
  EXPECT_TRUE(aclV4->getPktLenRange());
  EXPECT_EQ(aclV4->getPktLenRange().value().getMin(), 34);
  EXPECT_EQ(aclV4->getPktLenRange().value().getMax(), 1500);

  // set the ip frag option
  configV2.acls[0].ipFrag = cfg::IpFragMatch::MATCH_NOT_FRAGMENTED;
  configV2.acls[0].__isset.ipFrag = true;

  auto stateV5 = publishAndApplyConfig(stateV4, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV5);
  auto aclV5 = stateV5->getAcl(AclEntryID(101));
  EXPECT_NE(nullptr, aclV5);
  EXPECT_EQ(aclV5->getIpFrag().value(), cfg::IpFragMatch::MATCH_NOT_FRAGMENTED);
}

TEST(Acl, stateDelta) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls.resize(3);
  config.acls[0].id = 100;
  config.acls[0].action = cfg::AclAction::DENY;
  config.acls[0].__isset.srcIp = true;
  config.acls[0].srcIp = "192.168.0.1";
  config.acls[1].id = 200;
  config.acls[1].action = cfg::AclAction::PERMIT;
  config.acls[1].__isset.srcIp = true;
  config.acls[1].srcIp = "192.168.0.2";
  config.acls[2].id = 300;
  config.acls[2].action = cfg::AclAction::DENY;
  config.acls[2].__isset.srcIp = true;
  config.acls[2].srcIp = "192.168.0.3";
  config.acls[2].__isset.srcPort = true;
  config.acls[2].srcPort = 5;
  config.acls[2].__isset.dstPort = true;
  config.acls[2].dstPort = 8;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_EQ(stateV2, nullptr);

  // Only change one action
  config.acls[0].action = cfg::AclAction::PERMIT;
  auto stateV3 = publishAndApplyConfig(stateV1, &config, platform.get());
  StateDelta delta13(stateV1, stateV3);
  auto aclDelta13 = delta13.getAclsDelta();
  auto iter = aclDelta13.begin();
  EXPECT_EQ(iter->getOld()->getAction(), cfg::AclAction::DENY);
  EXPECT_EQ(iter->getNew()->getAction(), cfg::AclAction::PERMIT);
  ++iter;
  EXPECT_EQ(iter, aclDelta13.end());

  // Remove tail element
  config.acls.pop_back();
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  StateDelta delta34(stateV3, stateV4);
  auto aclDelta34 = delta34.getAclsDelta();
  iter = aclDelta34.begin();
  EXPECT_EQ(iter->getOld()->getSrcPort(), 5);
  EXPECT_EQ(iter->getOld()->getDstPort(), 8);
  EXPECT_EQ(iter->getNew(), nullptr);
  ++iter;
  EXPECT_EQ(iter, aclDelta34.end());
}

TEST(Acl, Icmp) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls.resize(1);
  config.acls[0].id = 100;
  config.acls[0].action = cfg::AclAction::DENY;
  config.acls[0].proto = 58;
  config.acls[0].__isset.proto = true;
  config.acls[0].icmpType = 128;
  config.acls[0].__isset.icmpType = true;
  config.acls[0].icmpCode = 0;
  config.acls[0].__isset.icmpCode = true;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl(AclEntryID(100));
  ASSERT_NE(nullptr, aclV1);
  EXPECT_EQ(AclEntryID(100), aclV1->getID());
  EXPECT_EQ(cfg::AclAction::DENY, aclV1->getAction());
  EXPECT_EQ(128, aclV1->getIcmpType().value());
  EXPECT_EQ(0, aclV1->getIcmpCode().value());

  // test config exceptions
  config.acls[0].proto = 4;
  EXPECT_THROW(
    publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  config.acls[0].__isset.proto = false;
  EXPECT_THROW(
    publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  config.acls[0].proto = 58;
  config.acls[0].__isset.proto = true;
  config.acls[0].__isset.icmpType = false;
  EXPECT_THROW(
    publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
}

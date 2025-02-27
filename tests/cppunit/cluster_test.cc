/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "cluster/cluster.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "server/server.h"

TEST(Cluster, CluseterSetNodes) {
  Status s;
  Cluster cluster(nullptr, {"127.0.0.1"}, 3002);

  const std::string invalid_fields =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave";
  s = cluster.SetClusterNodes(invalid_fields, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster nodes info");

  const std::string invalid_node_id =
      "07c37dfeb235213a872192d90877d0cd55635b9 127.0.0.1 30004 "
      "slave 07c37dfeb235213a872192d90877d0cd55635b92";
  s = cluster.SetClusterNodes(invalid_node_id, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node id");

  const std::string invalid_port =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 unknown "
      "master 07c37dfeb235213a872192d90877d0cd55635b91 5461-10922";
  s = cluster.SetClusterNodes(invalid_port, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node port");

  const std::string slave_has_no_master =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave -";
  s = cluster.SetClusterNodes(slave_has_no_master, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node id");

  const std::string master_has_master =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master 07c37dfeb235213a872192d90877d0cd55635b91 5461-10922";
  s = cluster.SetClusterNodes(master_has_master, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Invalid cluster node id");

  const std::string invalid_slot_range =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-0";
  s = cluster.SetClusterNodes(invalid_slot_range, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Slot is out of range");

  const std::string invalid_slot_id =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 54610";
  s = cluster.SetClusterNodes(invalid_slot_id, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Slot is out of range");

  const std::string overlapped_slot_id =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 0-126\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa2 127.0.0.1 30003 "
      "master - 0-16383";
  s = cluster.SetClusterNodes(overlapped_slot_id, 1, false);
  ASSERT_FALSE(s.IsOK());
  ASSERT_TRUE(s.Msg() == "Slot distribution is overlapped");

  const std::string right_nodes =
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 0 123-456 789 831 8192-16381 16382 16383";
  s = cluster.SetClusterNodes(right_nodes, 1, false);
  ASSERT_TRUE(s.IsOK());
  ASSERT_TRUE(cluster.GetVersion() == 1);
}

TEST(Cluster, CluseterGetNodes) {
  const std::string nodes =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave e7d1eecce10fd6bb5eb35b9f99a514335d9ba9ca\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-10922";
  Cluster cluster(nullptr, {"127.0.0.1"}, 30002);
  Status s = cluster.SetClusterNodes(nodes, 1, false);
  ASSERT_TRUE(s.IsOK());

  std::string output_nodes;
  s = cluster.GetClusterNodes(&output_nodes);
  ASSERT_TRUE(s.IsOK());

  std::vector<std::string> vnodes = Util::Split(output_nodes, "\n");

  for (unsigned i = 0; i < vnodes.size(); i++) {
    std::vector<std::string> node_fields = Util::Split(vnodes[i], " ");

    if (node_fields[0] == "07c37dfeb235213a872192d90877d0cd55635b91") {
      ASSERT_TRUE(node_fields.size() == 8);
      ASSERT_TRUE(node_fields[1] == "127.0.0.1:30004@40004");
      ASSERT_TRUE(node_fields[2] == "slave");
      ASSERT_TRUE(node_fields[3] == "e7d1eecce10fd6bb5eb35b9f99a514335d9ba9ca");
      ASSERT_TRUE(node_fields[6] == "1");
      ASSERT_TRUE(node_fields[7] == "connected");
    } else {
      ASSERT_TRUE(node_fields.size() == 9);
      ASSERT_TRUE(node_fields[1] == "127.0.0.1:30002@40002");
      ASSERT_TRUE(node_fields[2] == "myself,master");
      ASSERT_TRUE(node_fields[3] == "-");
      ASSERT_TRUE(node_fields[6] == "1");
      ASSERT_TRUE(node_fields[7] == "connected");
      ASSERT_TRUE(node_fields[8] == "5461-10922");
    }
  }
}

TEST(Cluster, CluseterGetSlotInfo) {
  const std::string nodes =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-10922";
  Cluster cluster(nullptr, {"127.0.0.1"}, 30002);
  Status s = cluster.SetClusterNodes(nodes, 1, false);
  ASSERT_TRUE(s.IsOK());

  std::vector<SlotInfo> slots_infos;
  s = cluster.GetSlotsInfo(&slots_infos);
  ASSERT_TRUE(s.IsOK());
  ASSERT_TRUE(slots_infos.size() == 1);
  SlotInfo info = slots_infos[0];
  ASSERT_TRUE(info.start == 5461);
  ASSERT_TRUE(info.end == 10922);
  ASSERT_TRUE(info.nodes.size() == 2);
  ASSERT_TRUE(info.nodes[0].port == 30002);
  ASSERT_TRUE(info.nodes[1].id == "07c37dfeb235213a872192d90877d0cd55635b91");
}

TEST(Cluster, TestDumpAndLoadClusterNodesInfo) {
  int64_t version = 2;
  const std::string nodes =
      "07c37dfeb235213a872192d90877d0cd55635b91 127.0.0.1 30004 "
      "slave 67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1\n"
      "67ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30002 "
      "master - 5461-10922\n"
      "17ed2db8d677e59ec4a4cefb06858cf2a1a89fa1 127.0.0.1 30003 master - 10923-16383";
  Cluster cluster(nullptr, {"127.0.0.1"}, 30002);
  Status s = cluster.SetClusterNodes(nodes, version, false);
  ASSERT_TRUE(s.IsOK());

  std::string nodes_filename = "nodes.conf";
  s = cluster.DumpClusterNodes(nodes_filename);
  ASSERT_TRUE(s.IsOK());
  Cluster new_cluster(nullptr, {"127.0.0.1"}, 30002);
  s = new_cluster.LoadClusterNodes(nodes_filename);
  ASSERT_TRUE(s.IsOK());
  ASSERT_EQ(version, new_cluster.GetVersion());
  std::vector<SlotInfo> slots_infos;
  s = new_cluster.GetSlotsInfo(&slots_infos);
  ASSERT_TRUE(s.IsOK());
  ASSERT_EQ(2, slots_infos.size());
  SlotInfo slot0_info = slots_infos[0];
  ASSERT_EQ(5461, slot0_info.start);
  ASSERT_EQ(10922, slot0_info.end);
  ASSERT_EQ(2, slot0_info.nodes.size());
  ASSERT_EQ(30002, slot0_info.nodes[0].port);
  ASSERT_EQ("07c37dfeb235213a872192d90877d0cd55635b91", slot0_info.nodes[1].id);
  SlotInfo slot1_info = slots_infos[1];
  ASSERT_EQ(10923, slot1_info.start);
  ASSERT_EQ(16383, slot1_info.end);
  ASSERT_EQ(1, slot1_info.nodes.size());
  ASSERT_EQ(30003, slot1_info.nodes[0].port);
  ASSERT_EQ("17ed2db8d677e59ec4a4cefb06858cf2a1a89fa1", slot1_info.nodes[0].id);

  unlink(nodes_filename.c_str());
}

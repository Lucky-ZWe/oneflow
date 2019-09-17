#include "oneflow/core/job/plan_util.h"
#include "oneflow/core/common/str_util.h"
#include "oneflow/core/graph/task_node.h"
#include "oneflow/core/persistence/tee_persistent_log_stream.h"

namespace oneflow {

namespace {

std::vector<std::vector<std::string>> GenNodeRank(const Plan& plan) {
  std::vector<std::vector<std::string>> ret(7);
  for (const TaskProto& task_proto : plan.task()) {
    if (task_proto.machine_id() != 0) { continue; }
    if (Global<IDMgr>::Get()->GetDeviceTypeFromThrdId(task_proto.thrd_id()) == DeviceType::kGPU) {
      continue;
    }
    if (task_proto.exec_sequence().exec_node_size() != 1) { continue; }
    std::string op_name =
        task_proto.exec_sequence().exec_node(0).kernel_conf().op_attribute().op_conf().name();
    std::string node_name = "task" + std::to_string(task_proto.task_id());
    if (op_name.find("WaitAndSendIds") != std::string::npos) {
      ret[0].push_back(node_name);
    } else if (op_name.find("ReentrantLock") != std::string::npos) {
      ret[1].push_back(node_name);
    } else if (op_name.find("Case") != std::string::npos) {
      ret[2].push_back(node_name);
    } else if (op_name.find("CriticalSection") != std::string::npos) {
      ret[3].push_back(node_name);
    } else if (op_name.find("SinkTick") != std::string::npos) {
      ret[4].push_back(node_name);
    } else if (op_name.find("Esac") != std::string::npos) {
      ret[5].push_back(node_name);
    } else if (op_name.find("CallbackNotify") != std::string::npos) {
      ret[6].push_back(node_name);
    }
  }
  return ret;
}

}  // namespace

RegstDescProto* PlanUtil::GetSoleProducedDataRegst(TaskProto* task_proto) {
  RegstDescProto* ret = nullptr;
  for (auto& pair : *task_proto->mutable_produced_regst_desc()) {
    RegstDescProto* regst_desc = &pair.second;
    if (regst_desc->regst_desc_type().has_data_regst_desc()) {
      CHECK_ISNULL(ret);
      CHECK_EQ(regst_desc->regst_desc_type().data_regst_desc().lbi2blob_desc_size(), 1);
      ret = regst_desc;
    }
  }
  CHECK_NOTNULL(ret);
  return ret;
}

std::function<const TaskProto&(int64_t)> PlanUtil::MakeGetterTaskProto4TaskId(const Plan& plan) {
  auto task_id2task_proto = std::make_shared<HashMap<int64_t, const TaskProto*>>();
  for (const TaskProto& task_proto : plan.task()) {
    task_id2task_proto->emplace(task_proto.task_id(), &task_proto);
  }
  return [task_id2task_proto](int64_t task_id) { return *task_id2task_proto->at(task_id); };
}

void PlanUtil::ToDotFile(const Plan& plan, const std::string& filepath) {
  size_t machine_num = Global<ResourceDesc>::Get()->TotalMachineNum();
  size_t gpu_device_num = Global<ResourceDesc>::Get()->GpuDeviceNum();
  std::vector<std::vector<std::vector<std::string>>> machine_id2device_id2node_list(machine_num);
  for (size_t i = 0; i < machine_num; ++i) {
    machine_id2device_id2node_list[i].resize(gpu_device_num);
  }
  std::vector<std::vector<std::string>> machine_id2host_node_list(machine_num);
  HashSet<int64_t> ctrl_regst_desc_ids;
  HashMap<int64_t, HashMap<int64_t, std::string>> task_id2consumer_regst_id2name;
  HashMap<int64_t, std::string> task_id2op_name;

  auto InsertNodeDefByTaskProto = [&](const TaskProto& task_proto, const std::string& node_def) {
    if (Global<IDMgr>::Get()->GetDeviceTypeFromThrdId(task_proto.thrd_id()) == DeviceType::kGPU) {
      int64_t device_id = Global<IDMgr>::Get()->GetGpuPhyIdFromThrdId(task_proto.thrd_id());
      machine_id2device_id2node_list[task_proto.machine_id()][device_id].push_back(node_def);
    } else {
      machine_id2host_node_list[task_proto.machine_id()].push_back(node_def);
    }
  };

  auto GenEdgeColorStr = [](const RegstDescTypeProto& type) {
    if (type.has_ctrl_regst_desc()) { return "fontcolor=\"gray65\",color=\"gray65\""; }
    return "fontcolor=\"gray15\",color=\"gray15\"";
  };

  auto IsEsac2ReentrantLockEdge = [](const std::string& src_name, const std::string& dst_name) {
    if (src_name.find("Esac") != std::string::npos
        && dst_name.find("ReentrantLock") != std::string::npos) {
      return true;
    }
    return false;
  };

  auto IsEsacNode = [](const std::string& name) {
    if (name.find("Esac") != std::string::npos) { return true; }
    return false;
  };

  auto log_stream = TeePersistentLogStream::Create(filepath);
  // task node
  for (const TaskProto& task_proto : plan.task()) {
    std::string node_def = "task" + std::to_string(task_proto.task_id()) + "[label=\"{{";
    // node_def += "<task_node_" + std::to_string(task_proto.task_id()) + ">";
    std::string op_name = "";
    for (const ExecNodeProto& exec_node : task_proto.exec_sequence().exec_node()) {
      op_name += (exec_node.kernel_conf().op_attribute().op_conf().name());
    }
    task_id2op_name[task_proto.task_id()] = op_name;
    node_def += op_name;
    size_t index = 0;
    for (const auto& pair : task_proto.produced_regst_desc()) {
      std::string regst_id = std::to_string(pair.second.regst_desc_id());
      if (index % 2 == 0) {
        node_def += "}|{";
      } else {
        node_def += "|";
      }
      // node_def += "<regst_desc_" + regst_id + ">";
      node_def += (pair.first + ":" + regst_id + ":" + std::to_string(pair.second.register_num()));
      ++index;
    }
    node_def += "}}";
    node_def +=
        ("\",tooltip=\"" + task_type2type_str.at(task_proto.task_type()) + "  "
         + std::to_string(task_proto.task_id()) + "-" + std::to_string(task_proto.machine_id())
         + ":" + std::to_string(task_proto.thrd_id()) + ":"
         + std::to_string(task_proto.parallel_ctx().parallel_id())
         + "\", shape=record, style=\"rounded,filled\""
         + ",colorscheme=set312, fillcolor=" + std::to_string((task_proto.job_id() % 12) + 1));
    if (IsEsacNode(op_name)) { node_def += ",width=5,height=1.5"; }
    node_def += "];\n";
    InsertNodeDefByTaskProto(task_proto, node_def);
    for (const auto& pair : task_proto.consumed_regst_desc_id()) {
      for (int64_t regst_desc_id : pair.second.regst_desc_id()) {
        task_id2consumer_regst_id2name[task_proto.task_id()][regst_desc_id] = pair.first;
      }
    }
  }

  log_stream << "digraph merged_plan_graph {\n";
  log_stream << "splines=\"ortho\";\n";
  log_stream << "rankdir=TB;\n";
  log_stream << "nodesep=1.3;\n";
  log_stream << "ranksep=1.3;\n";
  log_stream << "node[color=\"gray\"];\n";
  // sub graph
  for (size_t machine_id = 0; machine_id < machine_num; ++machine_id) {
    std::string machine_name = "machine_" + std::to_string(machine_id);
    log_stream << "subgraph cluster_" << machine_name << " { label = \"" << machine_name << "\";\n";
    log_stream << "style=\"rounded\";\n";
    for (const std::string& host_node_def : machine_id2host_node_list[machine_id]) {
      log_stream << host_node_def;
    }
    for (size_t device_id = 0; device_id < gpu_device_num; ++device_id) {
      std::string device_name = machine_name + "_device_" + std::to_string(device_id);
      log_stream << "subgraph cluster_" << device_name << " { label = \"" << device_name << "\";\n";
      log_stream << "color=\"skyblue\";\n";
      log_stream << "fillcolor=\"azure\";\n";
      log_stream << "style=\"rounded,filled\";\n";
      for (const auto& device_node_def : machine_id2device_id2node_list[machine_id][device_id]) {
        log_stream << device_node_def;
      }
      log_stream << "}\n";
    }
    log_stream << "}\n";
  }

  // produce/consume edge
  for (const TaskProto& task_proto : plan.task()) {
    for (const auto& pair : task_proto.produced_regst_desc()) {
      const RegstDescProto& regst = pair.second;
      std::string src_node = "task" + std::to_string(task_proto.task_id());
      // src_node += ":regst_desc_" + std::to_string(regst.regst_desc_id());
      for (int64_t consumer_task_id : regst.consumer_task_id()) {
        std::string dst_node = "task" + std::to_string(consumer_task_id);
        // dst_node +=  ":task_node_" + std::to_string(consumer_task_id);
        std::string consumer_regst_name =
            task_id2consumer_regst_id2name[consumer_task_id][regst.regst_desc_id()];
        std::string consumer_op_name = task_id2op_name[consumer_task_id];
        std::string producer_regst_name = pair.first;
        std::string producer_op_name = task_id2op_name[task_proto.task_id()];
        std::string tooltip = producer_op_name + " : " + producer_regst_name + " -> "
                              + consumer_op_name + " : " + consumer_regst_name;
        if (IsEsac2ReentrantLockEdge(producer_op_name, consumer_op_name)) {
          log_stream << dst_node << "->" << src_node
                     << "[arrowhead=\"invempty\",fontcolor=\"red\",color=\"red\",taillabel=\""
                     << consumer_regst_name << "\",tailtooltip=\"" << tooltip;
        } else {
          log_stream << src_node << "->" << dst_node << "["
                     << GenEdgeColorStr(regst.regst_desc_type()) << ",headlabel=\""
                     << consumer_regst_name << "\",headtooltip=\"" << tooltip;
        }
        log_stream << "\",tooltip=\"" << tooltip << "\",arrowsize=0.5,labeldistance=1.5,penwidth=2"
                   << "];\n";
      }
    }
  }
  // rank
  const auto& rank_list = GenNodeRank(plan);
  for (const auto& current_list : rank_list) {
    log_stream << "{ rank = same;";
    for (const auto& node_name : current_list) { log_stream << node_name << "; "; }
    log_stream << "}\n";
  }
  log_stream << "}\n";
}

}  // namespace oneflow
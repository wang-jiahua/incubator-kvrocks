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

#include "commander.h"
#include "error_constants.h"
#include "parse_util.h"
#include "server/server.h"
#include "storage/scripting.h"

namespace Redis {

class CommandEval : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override { return Status::OK(); }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    return Lua::evalGenericCommand(conn, args_, false, output);
  }
};

class CommandEvalSHA : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    if (args_[1].size() != 40) {
      *output = Redis::Error(errNoMatchingScript);
      return Status::OK();
    }
    return Lua::evalGenericCommand(conn, args_, true, output);
  }
};

class CommandEvalRO : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    return Lua::evalGenericCommand(conn, args_, false, output, true);
  }
};

class CommandEvalSHARO : public Commander {
 public:
  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    if (args_[1].size() != 40) {
      *output = Redis::Error(errNoMatchingScript);
      return Status::OK();
    }
    return Lua::evalGenericCommand(conn, args_, true, output, true);
  }
};

class CommandScript : public Commander {
 public:
  Status Parse(const std::vector<std::string> &args) override {
    subcommand_ = Util::ToLower(args[1]);
    return Status::OK();
  }

  Status Execute(Server *svr, Connection *conn, std::string *output) override {
    // There's a little tricky here since the script command was the write type
    // command but some subcommands like `exists` were readonly, so we want to allow
    // executing on slave here. Maybe we should find other way to do this.
    if (svr->IsSlave() && subcommand_ != "exists") {
      return {Status::NotOK, "READONLY You can't write against a read only slave"};
    }

    if (args_.size() == 2 && subcommand_ == "flush") {
      svr->ScriptFlush();
      auto s = svr->Propagate(Engine::kPropagateScriptCommand, args_);
      if (!s.IsOK()) {
        LOG(ERROR) << "Failed to propagate script command: " << s.Msg();
        return s;
      }
      *output = Redis::SimpleString("OK");
    } else if (args_.size() >= 2 && subcommand_ == "exists") {
      *output = Redis::MultiLen(args_.size() - 2);
      for (size_t j = 2; j < args_.size(); j++) {
        if (svr->ScriptExists(args_[j]).IsOK()) {
          *output += Redis::Integer(1);
        } else {
          *output += Redis::Integer(0);
        }
      }
    } else if (args_.size() == 3 && subcommand_ == "load") {
      std::string sha;
      auto s = Lua::createFunction(svr, args_[2], &sha, svr->Lua());
      if (!s.IsOK()) {
        return s;
      }

      *output = Redis::BulkString(sha);
    } else {
      return {Status::NotOK, "Unknown SCRIPT subcommand or wrong # of args"};
    }
    return Status::OK();
  }

 private:
  std::string subcommand_;
};

CommandKeyRange GetScriptEvalKeyRange(const std::vector<std::string> &args) {
  auto numkeys = ParseInt<int>(args[2], 10).ValueOr(0);

  return {3, 2 + numkeys, 1};
}

REDIS_REGISTER_COMMANDS(MakeCmdAttr<CommandEval>("eval", -3, "exclusive write no-script", GetScriptEvalKeyRange),
                        MakeCmdAttr<CommandEvalSHA>("evalsha", -3, "exclusive write no-script", GetScriptEvalKeyRange),
                        MakeCmdAttr<CommandEvalRO>("eval_ro", -3, "read-only no-script ro-script",
                                                   GetScriptEvalKeyRange),
                        MakeCmdAttr<CommandEvalSHARO>("evalsha_ro", -3, "read-only no-script ro-script",
                                                      GetScriptEvalKeyRange),
                        MakeCmdAttr<CommandScript>("script", -2, "exclusive no-script", 0, 0, 0), )

}  // namespace Redis

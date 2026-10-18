// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "libsiglus/bindings/registry.hpp"

#include "core/message_back.hpp"
#include "libsiglus/bindings/util.hpp"
#include "srbind/srbind.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <string>
#include <vector>

namespace libsiglus::binding {

namespace sb = srbind;
namespace sr = serilang;

namespace {

std::string DecodeFirstString(std::vector<sr::Value> raw_args) {
  auto packet = CallPacket::DecodeFrom(std::move(raw_args));
  if (packet.args.empty())
    return {};

  return AsString(packet.args[0]);
}

}  // namespace

void BindMsgbk(SiglusMachine& runtime) {
  sr::VM& vm = *runtime.vm;
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<MessageBack> msgbk_cls(m, "Msgbk", false);
  auto msgbk = msgbk_cls.inst("msgbk");

  msgbk.def(
      "insert_msg",
      [](MessageBack* msg, std::vector<sr::Value> raw_args) {
        msg->GoNextMsg();
        msg->AddMessage(DecodeFirstString(std::move(raw_args)));
      },
      sb::vararg);
  msgbk.def(
      "add_koe",
      [](MessageBack* msg, std::vector<sr::Value> raw_args) {
        auto packet = CallPacket::DecodeFrom(std::move(raw_args));
        if (packet.args.empty())
          return;

        const int koe_no = AsInt(packet.args[0]).value_or(0);
        if (koe_no < 0)
          return;

        int chara_no = -1;
        if (packet.args.size() > 1)
          chara_no = AsInt(packet.args[1]).value_or(-1);
        msg->AddKoe(koe_no, chara_no);
      },
      sb::vararg);
  msgbk.def(
      "add_namae",
      [](MessageBack* msg, std::vector<sr::Value> raw_args) {
        msg->AddNamae(DecodeFirstString(std::move(raw_args)));
      },
      sb::vararg);
  msgbk.def(
      "add_msg",
      [](MessageBack* msg, std::vector<sr::Value> raw_args) {
        msg->AddMessage(DecodeFirstString(std::move(raw_args)));
      },
      sb::vararg);
  msgbk.def("go_next_msg", &MessageBack::GoNextMsg);
  msgbk.def(
      "insert_img",
      [](MessageBack* msg, std::vector<sr::Value> raw) {
        const auto packet = CallPacket::DecodeFrom(std::move(raw));
        if (packet.args.empty())
          return;
        msg->InsertImage(AsString(packet.args[0]),
                         packet.args.size() > 1
                             ? RequireInt(packet.args[1], "backlog image x")
                             : 0);
      },
      sb::vararg);
}

RLVM_REGISTER(SiglusBindingRegistry, "msgbk", BindMsgbk)

}  // namespace libsiglus::binding

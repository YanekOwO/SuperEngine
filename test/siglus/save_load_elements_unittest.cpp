// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
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

#include <gtest/gtest.h>

#include "libsiglus/element.hpp"
#include "libsiglus/element_parser.hpp"

#include <initializer_list>
#include <string>
#include <vector>

namespace libsiglus {
namespace {
using namespace elm;

class SiglusSaveLoadElementTest : public ::testing::Test {
 protected:
  void Check(std::initializer_list<int> codes,
             const std::string& name,
             Type type) {
    std::vector<Property> properties;
    std::vector<Command> commands;
    std::vector<Type> arguments;
    std::vector<std::string> warnings;
    ElementParser parser(
        properties, properties, commands, commands, arguments, 0,
        [] { return 0; },
        [&](std::string text) { warnings.push_back(std::move(text)); });
    ElementCode element(codes);
    element.ForceBind({0, {}, Type::None});
    auto chain = parser.Parse(element);
    EXPECT_TRUE(warnings.empty());
    ASSERT_GE(chain.nodes.size(), 2);
    const auto* call = std::get_if<Call>(&chain.nodes.back().var);
    ASSERT_NE(call, nullptr);
    const auto* member =
        std::get_if<Member>(&chain.nodes[chain.nodes.size() - 2].var);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->name, name);
    EXPECT_EQ(chain.GetType(), type);
  }
};

#define SAVE_ELEMENT_TEST(TestName, Name, ReturnType, ...)                \
  TEST_F(SiglusSaveLoadElementTest, TestName) {                           \
    /* A numeric persistence element resolves its callable and result. */ \
    Check({__VA_ARGS__}, Name, Type::ReturnType);                         \
  }

SAVE_ELEMENT_TEST(GlobalSavepoint, "savepoint", Int, 36)
SAVE_ELEMENT_TEST(GlobalClearSavepoint, "clear_savepoint", None, 113)
SAVE_ELEMENT_TEST(GlobalCheckSavepoint, "check_savepoint", Int, 112)
SAVE_ELEMENT_TEST(ScriptSetAutoSavepointOff,
                  "set_auto_savepoint_off",
                  None,
                  64,
                  38)
SAVE_ELEMENT_TEST(ScriptSetAutoSavepointOn,
                  "set_auto_savepoint_on",
                  None,
                  64,
                  39)
SAVE_ELEMENT_TEST(GlobalSetTitle, "set_title", None, 74)
SAVE_ELEMENT_TEST(GlobalGetTitle, "get_title", String, 75)
SAVE_ELEMENT_TEST(SyscomGetCurrentSaveSceneTitle,
                  "get_current_save_title",
                  String,
                  63,
                  294)
SAVE_ELEMENT_TEST(SyscomGetCurrentSaveMessage,
                  "get_current_save_message",
                  String,
                  63,
                  295)
SAVE_ELEMENT_TEST(SyscomCallSaveMenu, "open_save", None, 63, 1)
SAVE_ELEMENT_TEST(SyscomSetSaveEnableFlag, "set_enable_save", None, 63, 251)
SAVE_ELEMENT_TEST(SyscomGetSaveEnableFlag, "get_enable_save", Int, 63, 252)
SAVE_ELEMENT_TEST(SyscomSetSaveExistFlag, "set_exist_save", None, 63, 253)
SAVE_ELEMENT_TEST(SyscomGetSaveExistFlag, "get_exist_save", Int, 63, 254)
SAVE_ELEMENT_TEST(SyscomCheckSaveEnable, "is_save_enable", Int, 63, 255)
SAVE_ELEMENT_TEST(SyscomCallLoadMenu, "open_load", None, 63, 2)
SAVE_ELEMENT_TEST(SyscomSetLoadEnableFlag, "set_enable_load", None, 63, 258)
SAVE_ELEMENT_TEST(SyscomGetLoadEnableFlag, "get_enable_load", Int, 63, 259)
SAVE_ELEMENT_TEST(SyscomSetLoadExistFlag, "set_exist_load", None, 63, 260)
SAVE_ELEMENT_TEST(SyscomGetLoadExistFlag, "get_exist_load", Int, 63, 261)
SAVE_ELEMENT_TEST(SyscomCheckLoadEnable, "is_load_enable", Int, 63, 262)
SAVE_ELEMENT_TEST(SyscomSave, "save", Int, 63, 249)
SAVE_ELEMENT_TEST(SyscomLoad, "load", None, 63, 256)
SAVE_ELEMENT_TEST(SyscomQuickSave, "qsave", Int, 63, 18)
SAVE_ELEMENT_TEST(SyscomQuickLoad, "qload", None, 63, 20)
SAVE_ELEMENT_TEST(SyscomEndSave, "endsave", Int, 63, 271)
SAVE_ELEMENT_TEST(SyscomEndLoad, "endload", None, 63, 269)
SAVE_ELEMENT_TEST(SyscomInnerSave, "inner_save", Int, 63, 272)
SAVE_ELEMENT_TEST(SyscomInnerLoad, "inner_load", Int, 63, 273)
SAVE_ELEMENT_TEST(SyscomClearInnerSave, "clear_inner_save", Int, 63, 276)
SAVE_ELEMENT_TEST(SyscomCopyInnerSave, "copy_inner_save", Int, 63, 274)
SAVE_ELEMENT_TEST(SyscomCheckInnerSave, "check_inner_save", Int, 63, 275)
SAVE_ELEMENT_TEST(GlobalSelpoint, "selpoint", None, 1)
SAVE_ELEMENT_TEST(GlobalClearSelpoint, "clear_selpoint", None, 111)
SAVE_ELEMENT_TEST(GlobalCheckSelpoint, "check_selpoint", Int, 110)
SAVE_ELEMENT_TEST(GlobalStackSelpoint, "stack_selpoint", None, 149)
SAVE_ELEMENT_TEST(GlobalDropSelpoint, "drop_selpoint", None, 150)
SAVE_ELEMENT_TEST(SyscomReturnToSel, "return_to_sel", None, 63, 228)
SAVE_ELEMENT_TEST(SyscomSetReturnToSelEnableFlag,
                  "set_enable_retsel",
                  None,
                  63,
                  230)
SAVE_ELEMENT_TEST(SyscomGetReturnToSelEnableFlag,
                  "get_enable_retsel",
                  Int,
                  63,
                  231)
SAVE_ELEMENT_TEST(SyscomSetReturnToSelExistFlag,
                  "set_exist_retsel",
                  None,
                  63,
                  232)
SAVE_ELEMENT_TEST(SyscomGetReturnToSelExistFlag,
                  "get_exist_retsel",
                  Int,
                  63,
                  233)
SAVE_ELEMENT_TEST(SyscomCheckReturnToSelEnable,
                  "is_retsel_enable",
                  Int,
                  63,
                  234)
SAVE_ELEMENT_TEST(SyscomMsgBackLoad, "msgbk_load", None, 63, 310)
SAVE_ELEMENT_TEST(SyscomGetSaveCnt, "get_save_count", Int, 63, 68)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveCnt, "get_qsave_count", Int, 63, 168)
SAVE_ELEMENT_TEST(SyscomGetSaveNewNo, "get_new_save_no", Int, 63, 79)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveNewNo, "get_new_qsave_no", Int, 63, 170)
SAVE_ELEMENT_TEST(SyscomGetSaveExist, "is_save_exist", Int, 63, 69)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveExist, "is_qsave_exist", Int, 63, 169)
SAVE_ELEMENT_TEST(SyscomGetEndSaveExist, "is_endsave_exist", Int, 63, 270)
SAVE_ELEMENT_TEST(SyscomGetSaveYear, "get_save_year", Int, 63, 70)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveYear, "get_qsave_year", Int, 63, 171)
SAVE_ELEMENT_TEST(SyscomGetSaveMonth, "get_save_month", Int, 63, 71)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveMonth, "get_qsave_month", Int, 63, 172)
SAVE_ELEMENT_TEST(SyscomGetSaveDay, "get_save_day", Int, 63, 72)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveDay, "get_qsave_day", Int, 63, 173)
SAVE_ELEMENT_TEST(SyscomGetSaveWeekday, "get_save_weekday", Int, 63, 73)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveWeekday, "get_qsave_weekday", Int, 63, 174)
SAVE_ELEMENT_TEST(SyscomGetSaveHour, "get_save_hour", Int, 63, 74)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveHour, "get_qsave_hour", Int, 63, 175)
SAVE_ELEMENT_TEST(SyscomGetSaveMinute, "get_save_minute", Int, 63, 75)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveMinute, "get_qsave_minute", Int, 63, 176)
SAVE_ELEMENT_TEST(SyscomGetSaveSecond, "get_save_second", Int, 63, 76)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveSecond, "get_qsave_second", Int, 63, 177)
SAVE_ELEMENT_TEST(SyscomGetSaveMillisecond, "get_save_millisecond", Int, 63, 77)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveMillisecond,
                  "get_qsave_millisecond",
                  Int,
                  63,
                  178)
SAVE_ELEMENT_TEST(SyscomGetSaveTitle, "get_save_title", String, 63, 78)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveTitle, "get_qsave_title", String, 63, 179)
SAVE_ELEMENT_TEST(SyscomGetSaveMessage, "get_save_message", String, 63, 129)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveMessage,
                  "get_qsave_message",
                  String,
                  63,
                  130)
SAVE_ELEMENT_TEST(SyscomGetSaveFullMessage,
                  "get_save_full_message",
                  String,
                  63,
                  324)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveFullMessage,
                  "get_qsave_full_message",
                  String,
                  63,
                  325)
SAVE_ELEMENT_TEST(SyscomGetSaveComment, "get_save_comment", String, 63, 131)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveComment,
                  "get_qsave_comment",
                  String,
                  63,
                  132)
SAVE_ELEMENT_TEST(SyscomGetSaveAppendDir,
                  "get_save_append_dir",
                  String,
                  63,
                  320)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveAppendDir,
                  "get_qsave_append_dir",
                  String,
                  63,
                  322)
SAVE_ELEMENT_TEST(SyscomGetSaveAppendName,
                  "get_save_append_name",
                  String,
                  63,
                  321)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveAppendName,
                  "get_qsave_append_name",
                  String,
                  63,
                  323)
SAVE_ELEMENT_TEST(SyscomSetSaveComment, "set_save_comment", None, 63, 180)
SAVE_ELEMENT_TEST(SyscomGetSaveValue, "get_save_value", None, 63, 183)
SAVE_ELEMENT_TEST(SyscomSetSaveValue, "set_save_value", None, 63, 182)
SAVE_ELEMENT_TEST(SyscomSetQuickSaveComment, "set_qsave_comment", None, 63, 181)
SAVE_ELEMENT_TEST(SyscomGetQuickSaveValue, "get_qsave_value", None, 63, 184)
SAVE_ELEMENT_TEST(SyscomSetQuickSaveValue, "set_qsave_value", None, 63, 185)
SAVE_ELEMENT_TEST(SyscomCopySave, "copy_save", Int, 63, 67)
SAVE_ELEMENT_TEST(SyscomChangeSave, "change_save", Int, 63, 22)
SAVE_ELEMENT_TEST(SyscomDeleteSave, "delete_save", Int, 63, 19)
SAVE_ELEMENT_TEST(SyscomCopyQuickSave, "copy_qsave", Int, 63, 128)
SAVE_ELEMENT_TEST(SyscomChangeQuickSave, "change_qsave", Int, 63, 66)
SAVE_ELEMENT_TEST(SyscomDeleteQuickSave, "delete_qsave", Int, 63, 65)
SAVE_ELEMENT_TEST(SyscomSetSaveloadAlertOnoff,
                  "set_saveload_alert_onoff",
                  None,
                  63,
                  80)
SAVE_ELEMENT_TEST(SyscomSetSaveloadAlertOnoffDefault,
                  "set_saveload_alert_onoff_default",
                  None,
                  63,
                  108)
SAVE_ELEMENT_TEST(SyscomGetSaveloadAlertOnoff,
                  "get_saveload_alert_onoff",
                  Int,
                  63,
                  10)
SAVE_ELEMENT_TEST(GlobalCapture, "capture", None, 80)
SAVE_ELEMENT_TEST(GlobalCaptureFromFile, "capture_from_file", None, 163)
SAVE_ELEMENT_TEST(GlobalCaptureFree, "capture_free", None, 81)
SAVE_ELEMENT_TEST(ObjectCreateSaveThumb,
                  "create_save_thumb",
                  None,
                  38,
                  2,
                  -1,
                  0,
                  116)
SAVE_ELEMENT_TEST(GlobalCaptureForLocalSave, "capture_for_local_save", Int, 144)
SAVE_ELEMENT_TEST(ObjectCreateCaptureThumb,
                  "create_capture_thumb",
                  None,
                  38,
                  2,
                  -1,
                  0,
                  170)
SAVE_ELEMENT_TEST(SyscomCreateCaptureBuffer,
                  "create_capture_buffer",
                  None,
                  63,
                  286)
SAVE_ELEMENT_TEST(SyscomDestroyCaptureBuffer,
                  "destroy_capture_buffer",
                  None,
                  63,
                  287)
SAVE_ELEMENT_TEST(SyscomCaptureToCaptureBuffer,
                  "capture_to_capture_buffer",
                  None,
                  63,
                  290)
SAVE_ELEMENT_TEST(SyscomSaveCaptureBufferToFile,
                  "save_capture_buffer_to_file",
                  Int,
                  63,
                  314)
SAVE_ELEMENT_TEST(SyscomLoadFlagFromCaptureFile,
                  "load_flag_from_capture_file",
                  Int,
                  63,
                  315)
SAVE_ELEMENT_TEST(SyscomCaptureAndSaveBufferToPng,
                  "capture_to_png",
                  None,
                  63,
                  316)
SAVE_ELEMENT_TEST(SystemCheckFileExistSaveDir,
                  "check_save_file_exist",
                  Int,
                  92,
                  12)
SAVE_ELEMENT_TEST(GlobalOwari, "owari", None, 2)
SAVE_ELEMENT_TEST(SyscomEndGame, "end_game", None, 63, 242)
SAVE_ELEMENT_TEST(SyscomReturnToMenu, "return_to_menu", None, 63, 235)
SAVE_ELEMENT_TEST(GlobalReturnmenu, "returnmenu", None, 3)
SAVE_ELEMENT_TEST(SyscomGetTotalPlayTime, "get_total_playtime", None, 63, 199)
SAVE_ELEMENT_TEST(SyscomSetTotalPlayTime, "set_total_playtime", None, 63, 229)
SAVE_ELEMENT_TEST(SyscomInitSyscomFlag, "init_flags", None, 63, 5)

#undef SAVE_ELEMENT_TEST

}  // namespace
}  // namespace libsiglus

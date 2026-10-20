// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "platforms/sdl/sdl_implementor.hpp"

#include "platforms/platform_factory.hpp"
#include "utilities/file.hpp"
#include "utilities/gettext.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <iostream>
#include <mutex>

namespace fs = std::filesystem;

namespace {

struct FileDialogResult {
  std::mutex mutex;
  fs::path path;
  std::atomic<bool> done{false};
};

void SDLCALL OnFileChosen(void* userdata,
                          const char* const* filelist,
                          int /*filter*/) {
  auto* result = static_cast<FileDialogResult*>(userdata);
  {
    std::lock_guard<std::mutex> lock(result->mutex);
    if (!filelist)
      std::cerr << "File dialog error: " << SDL_GetError() << std::endl;
    else if (filelist[0])
      result->path = filelist[0];
  }
  result->done = true;
}

// SDL file dialogs are asynchronous. On some platforms the callback
// fires during event processing on this thread. On other platforms it fires
// from a helper thread. Thus the wait loop pumps events until the callback
// sets the flag.
fs::path WaitForFileDialog(FileDialogResult& result) {
  while (!result.done) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  std::lock_guard<std::mutex> lock(result.mutex);
  return result.path;
}

fs::path RunFolderDialog() {
  FileDialogResult result;
  SDL_ShowOpenFolderDialog(OnFileChosen, &result, /*window=*/nullptr,
                           /*default_location=*/nullptr, /*allow_many=*/false);
  return WaitForFileDialog(result);
}

}  // namespace

fs::path SdlImplementor::SelectFile(bool save,
                                    const std::string& title,
                                    const fs::path& initial_path,
                                    const std::string& extension) {
  FileDialogResult result;
  SDL_DialogFileFilter filter{extension.c_str(), extension.c_str()};
  const auto props = SDL_CreateProperties();
  if (!props)
    return {};
  SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, &filter);
  SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
  SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                        title.c_str());
  SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                        initial_path.string().c_str());
  SDL_ShowFileDialogWithProperties(
      save ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE, OnFileChosen,
      &result, props);
  auto path = WaitForFileDialog(result);
  SDL_DestroyProperties(props);
  return path;
}

fs::path SdlImplementor::SelectGameDirectory() {
  // This function runs before System exists. The dialog needs the video
  // subsystem.
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    std::cerr << "Video initialization failed: " << SDL_GetError() << std::endl;
    return {};
  }

  for (;;) {
    fs::path dir = RunFolderDialog();
    if (dir.empty())
      return {};

    auto detectGame = [](const fs::path& dir) -> bool {
      if (!CorrectPath(dir, "Gameexe.ini").empty())  // likely reallive
        return true;
      if (!CorrectPath(dir, "Gameexe.dat").empty())  // likely siglus
        return true;
      return false;
    };
    if (detectGame(dir))
      return dir;

    if (!AskUserPrompt(
            _("Select Game Directory"),
            dir.string() +
                _(" doesn't contain a Gameexe.ini or Gameexe.dat. Try again?"),
            _("Retry"), _("Cancel")))
      return {};
  }
}

void SdlImplementor::ReportFatalError(const std::string& message_text,
                                      const std::string& informative_text) {
  const std::string message = message_text + "\n\n" + informative_text;
  if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "rlvm", message.c_str(),
                                nullptr))
    std::cerr << message << std::endl;
}

bool SdlImplementor::AskUserPrompt(const std::string& message_text,
                                   const std::string& informative_text,
                                   const std::string& true_button,
                                   const std::string& false_button) {
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, false_button.c_str()},
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, true_button.c_str()},
  };
  const std::string message = message_text + "\n\n" + informative_text;
  const SDL_MessageBoxData data = {
      SDL_MESSAGEBOX_WARNING, nullptr, "rlvm", message.c_str(),
      SDL_arraysize(buttons), buttons, nullptr};
  int button_id = -1;
  if (!SDL_ShowMessageBox(&data, &button_id))
    return false;
  return button_id == 1;
}

RLVM_REGISTER(PlatformFactory, "sdl", []() {
  return std::make_shared<SdlImplementor>();
});

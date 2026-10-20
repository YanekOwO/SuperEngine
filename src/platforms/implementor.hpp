// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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
//
// -----------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <memory>
#include <string>

/**
 * @file
 * Interface declaration for platform-specific GUI implementations in RLVM.
 */

/**
 * @class IPlatformImplementor
 * @brief Abstract base class for platform-specific GUI implementations.
 *
 * This class provides an interface for all platform-specific GUI tasks,
 * such as file dialogs, and error reporting.
 *
 * Example implementation and registration:
 * @code
 * class MyPlatform : public IPlatformImplementor {
 * public:
 *   std::filesystem::path SelectGameDirectory() override {
 *     // Implementation for selecting game directory
 *   }
 *
 *   void ReportFatalError(const std::string& message_text,
 *                         const std::string& informative_text) override {
 *     // Implementation to report error
 *   }
 *
 *   bool AskUserPrompt(const std::string& message_text,
 *                      const std::string& informative_text,
 *                      const std::string& true_button,
 *                      const std::string& false_button) override {
 *     // Implementation to ask user prompt
 *   }
 * };
 *
 * namespace {
 *   PlatformFactory::Registrar registrar("my_platform_name", []() {
 *     return std::make_shared<MyPlatform>();
 *   });
 * }
 * @endcode
 *
 * To use a custom platform implementation at runtime, start RLVM with:
 * @verbatim
 * rlvm --platform=my_platform_name
 * @endverbatim
 */
class IPlatformImplementor {
 public:
  virtual ~IPlatformImplementor() = default;

  /**
   * @brief Presents a dialog for the user to select the game directory.
   * @return Path to the selected game directory root.
   */
  virtual std::filesystem::path SelectGameDirectory() = 0;

  // An empty path represents cancellation or an unavailable dialog backend.
  virtual std::filesystem::path SelectFile(
      bool save,
      const std::string& title,
      const std::filesystem::path& initial_path,
      const std::string& extension) {
    return {};
  }

  /**
   * @brief Reports a fatal error with details before the program exits.
   * @param message_text Main error message to be displayed.
   * @param informative_text Additional information or context about the error.
   */
  virtual void ReportFatalError(const std::string& message_text,
                                const std::string& informative_text) = 0;

  /**
   * @brief Presents a yes/no dialog to the user and captures the response.
   * @param message_text Main message text to be displayed in the dialog.
   * @param informative_text Additional information or details about the
   * message.
   * @param true_button Text for the 'Yes' button.
   * @param false_button Text for the 'No' button.
   * @return True if the user clicks 'Yes', false if 'No'.
   */
  virtual bool AskUserPrompt(const std::string& message_text,
                             const std::string& informative_text,
                             const std::string& true_button,
                             const std::string& false_button) = 0;
};

/**
 * @typedef PlatformImpl_t
 * @brief Smart pointer type for handling platform implementations.
 */
using PlatformImpl_t = std::shared_ptr<IPlatformImplementor>;

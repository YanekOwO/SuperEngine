// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "log/logger.hpp"

#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

struct Logger::Ctx {
  using logger_impl_t = typename boost::log::sources::logger;
  logger_impl_t logger;
};

Logger::Logger() : ctx_(std::make_unique<Ctx>()) { ClearAttributes(); }

Logger::~Logger() = default;

void Logger::ClearAttributes() const {
  ctx_->logger.remove_all_attributes();
  ctx_->logger.add_attribute(
      "Channel", boost::log::attributes::constant<std::string>("diagnostic"));
}

void Logger::AddSeverity(Severity severity) const {
  ctx_->logger.add_attribute(
      "Severity", boost::log::attributes::constant<Severity>(severity));
}

void Logger::AddScope(std::string scope) const {
  ctx_->logger.add_attribute(
      "Scope", boost::log::attributes::constant<std::string>(scope));
}

void Logger::Log(const std::string& msg) const {
  auto& logger = ctx_->logger;

  boost::log::record rec = logger.open_record();
  if (rec) {
    boost::log::record_ostream strm(rec);

    strm << msg;
    strm.flush();

    logger.push_record(boost::move(rec));
  }
}

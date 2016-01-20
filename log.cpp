/*
 * Copyright 2016 Alan Chen <cchck91@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/keywords/facility.hpp>
#include <boost/log/keywords/use_impl.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/global_logger_storage.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/empty_deleter.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/make_shared.hpp>
#include <boost/phoenix/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "log.h"

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace sinks = boost::log::sinks;
namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;

namespace zephyr {

BOOST_LOG_GLOBAL_LOGGER(logger, src::severity_logger_mt<LogLevel>)

BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "TimeStamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", LogLevel)

static std::string ToSimpleLevel(logging::value_ref<LogLevel> const &l) {
  if (l) {
    switch (l.get()) {
    case LOG_VERBOSE: return "V";
    case LOG_DEBUG: return "\033[1;34mD\033[0m";
    case LOG_INFO: return "\033[01;36mI\033[0m";
    case LOG_WARN: return "\033[01;35mW\033[0m";
    case LOG_ERROR: return "\033[01;31mE\033[0m";
    case LOG_FATAL: return "\033[01;31mF\033[0m";
    default: return "";
    }
  }
  return "";
}

BOOST_LOG_GLOBAL_LOGGER_INIT(logger, src::severity_logger_mt) {
  src::severity_logger_mt<LogLevel> logger;

  logger.add_attribute("TimeStamp", attrs::local_clock());

  // add a text sink
  typedef sinks::synchronous_sink<sinks::text_ostream_backend> text_sink;
  boost::shared_ptr<text_sink> sink_txt = boost::make_shared<text_sink>();


  // add "console" output stream to our sink
  sink_txt->locked_backend()->add_stream(
      boost::shared_ptr<std::ostream>(&std::clog,
                                      logging::v2_mt_posix::empty_deleter()));

  // specify the format of the log message
  logging::formatter formatter =
      expr::stream << expr::format_date_time(timestamp, "%Y-%m-%d-%H:%M:%S")
                   << " "
                   << boost::phoenix::bind(&ToSimpleLevel,
                                           expr::attr<LogLevel>("Severity"))
                   << "/"
                   << "\033[01;37m" << expr::attr<std::string>("Tag")
                   << "\033[0m"
                   << "(" << expr::attr<pid_t>("ProcessID") << ")"
                   << ": " << expr::smessage;
  sink_txt->set_formatter(formatter);

  // "register" our sink
  logging::core::get()->add_sink(sink_txt);

#ifdef CONFIG_SYSLOG
  typedef sinks::synchronous_sink<sinks::syslog_backend> syslog_sink;
  boost::shared_ptr<sinks::syslog_backend> sink_syslog(
      new sinks::syslog_backend(keywords::facility = sinks::syslog::user,
                                keywords::use_impl = sinks::syslog::native));

  sinks::syslog::custom_severity_mapping<LogLevel> mapping("LogLevel");
  mapping[LOG_VERBOSE] = sinks::syslog::debug;
  mapping[LOG_DEBUG] = sinks::syslog::debug;
  mapping[LOG_INFO] = sinks::syslog::info;
  mapping[LOG_WARN] = sinks::syslog::warning;
  mapping[LOG_ERROR] = sinks::syslog::error;
  mapping[LOG_FATAL] = sinks::syslog::critical;
  sink_syslog->set_severity_mapper(mapping);

  // "register" our sink
  logging::core::get()->add_sink(boost::make_shared<syslog_sink>(sink_syslog));
#endif

  return logger;
}

#define LOG(severity) BOOST_LOG_SEV(logger::get(), severity)

static inline void PrintLogInternal(int priority, const std::string &text) {
  LOG(static_cast<LogLevel>(priority)) << text;
}

void PrintLog(int priority, const char *tag, const char *fmt, ...) {
  std::string result;

  va_list ap;
  va_start(ap, fmt);

  char *tmp = 0;
  vasprintf(&tmp, fmt, ap);
  va_end(ap);

  if (tmp == 0) {
    return;
  } else {
    result = tmp;
    free(tmp);
  }

  BOOST_LOG_SCOPED_LOGGER_TAG(logger::get(), "Tag", tag)
  BOOST_LOG_SCOPED_LOGGER_TAG(logger::get(), "ProcessID", getpid());

  typedef std::vector<std::string> split_vector_type;
  split_vector_type split_strings;
  boost::split(split_strings, result, boost::is_any_of("\n"));

  split_vector_type::const_iterator iter = split_strings.begin();
  for (; iter != split_strings.end(); ++iter) {
    PrintLogInternal(priority, *iter);
  }
}

}  // namespace zephyr

#pragma once

// This header is included in every build, even those with SLIC3R_PCH disabled

#include <boost/log/trivial.hpp>

#undef BOOST_LOG_TRIVIAL
#define BOOST_LOG_TRIVIAL(lvl)\
BOOST_LOG_STREAM_WITH_PARAMS(::boost::log::trivial::logger::get(),\
(::boost::log::keywords::severity = ::boost::log::trivial::lvl)) \
<< __FUNCTION__ << ":" << __LINE__ << ": _MSG_"


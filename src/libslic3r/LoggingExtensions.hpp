#ifndef ORCASLICER_LOGGINGEXTENSIONS_HPP
#define ORCASLICER_LOGGINGEXTENSIONS_HPP
#include <boost/log/expressions/keyword.hpp>
#include <boost/scope_exit.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/sources/global_logger_storage.hpp>

namespace Slic3r {
BOOST_LOG_ATTRIBUTE_KEYWORD_IMPL(function_name, "FunctionName", std::string, logging_tags)
BOOST_LOG_ATTRIBUTE_KEYWORD_IMPL(line_number, "LineNumber", int, logging_tags)
BOOST_LOG_ATTRIBUTE_KEYWORD_IMPL(file_name, "FileName", std::string, logging_tags)

namespace logging_keywords {
BOOST_PARAMETER_KEYWORD(Slic3r::logging_tag, function_name)
BOOST_PARAMETER_KEYWORD(Slic3r::logging_tag, line_number)
BOOST_PARAMETER_KEYWORD(Slic3r::logging_tag, file_name)
}

namespace trivial {
using namespace boost::log::sources;
namespace trivial = boost::log::trivial;

template<typename BaseT>
class function_info_feature : public BaseT
{
public:
    typedef typename BaseT::char_type char_type;
    typedef typename BaseT::threading_model threading_model;

    function_info_feature(){}
    function_info_feature(function_info_feature const& that) : BaseT(static_cast<BaseT const&>(that)) {}
    template<typename ArgsT>
    function_info_feature(ArgsT const& args) : BaseT(args) {}

protected:
    template<typename ArgsT>
    boost::log::record open_record_unlocked(ArgsT const& args)
    {
        const std::string function_name = args[logging_keywords::function_name | std::string()];
        const int line_number = args[logging_keywords::line_number | -1];
        const std::string file_name = args[logging_keywords::file_name | std::string()];

        boost::log::attribute_set& attrs = BaseT::attributes();
        std::vector<boost::log::attribute_set::iterator> to_delete;

        if (!function_name.empty()) {
            std::pair<boost::log::attribute_set::iterator, bool> res =
                BaseT::add_attribute_unlocked(logging_tags::function_name::get_name(), boost::log::attributes::constant(function_name));

            if (res.second)
                to_delete.push_back(std::move(res.first));
        }

        if (line_number != -1) {
            std::pair<boost::log::attribute_set::iterator, bool> res =
                BaseT::add_attribute_unlocked(logging_tags::line_number::get_name(), boost::log::attributes::constant(line_number));

            if (res.second)
                to_delete.push_back(std::move(res.first));
        }

        if (!file_name.empty()) {
            std::pair<boost::log::attribute_set::iterator, bool> res =
                BaseT::add_attribute_unlocked(logging_tags::file_name::get_name(), boost::log::attributes::constant(file_name));

            if (res.second)
                to_delete.push_back(std::move(res.first));
        }

        BOOST_SCOPE_EXIT_TPL((&to_delete)(&attrs))
        {
            for (auto it : to_delete)
                attrs.erase(it);
        } BOOST_SCOPE_EXIT_END

        return BaseT::open_record_unlocked(args);
    }
};

struct function_info : boost::mpl::quote1<function_info_feature>{};

class severity_and_function_info_logger_mt : public basic_composite_logger<
        char,
        severity_and_function_info_logger_mt,
        multi_thread_model< boost::log::aux::light_rw_mutex >,
        features< severity< trivial::severity_level >, function_info >
>
{
    BOOST_LOG_FORWARD_LOGGER_MEMBERS_TEMPLATE(severity_and_function_info_logger_mt)
};

struct logger
{
    typedef severity_and_function_info_logger_mt logger_type;

    static logger_type& get()
    {
        return aux::logger_singleton< logger >::get();
    }

    enum registration_line_t { registration_line = __LINE__ };
    static const char* registration_file() { return __FILE__; }
    static logger_type construct_logger()
    {
        return logger_type(boost::log::keywords::severity = trivial::info);
    }
};
}
}

#endif // ORCASLICER_LOGGINGEXTENSIONS_HPP

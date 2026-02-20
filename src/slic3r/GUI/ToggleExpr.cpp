
#include "ToggleExpr.hpp"

namespace Slic3r { namespace GUI {

void ToggleExpr::get_reasons(std::vector<std::string>& reasons, const ToggleExpr& expr)
{
    auto& expr_reasons = expr.m_reasons;
    if (expr_reasons.empty())
        return;
    if (expr.m_unformatted_reason) {
        // If the reason is unformatted, there should only be one reason in the list,
        // but there can be cases where there is a single formatted reason passed from a different ToggleExpr
        assert(expr_reasons.size() == 1);
        reasons.emplace_back(expr.get_prefix() + expr_reasons[0] + expr.get_postfix());
        return;
    }

    reasons.insert(reasons.end(), expr_reasons.begin(), expr_reasons.end());
}
std::string ToggleExpr::get_prefix() const
{
    if (!m_has_prefixes)
        return "";
    std::string p = m_is_not ? m_inverted_prefix : m_standard_prefix;
    if (!p.empty() && p.back() != ' ')
        p += " ";
    return p;
}

std::string ToggleExpr::get_postfix() const
{
    if (m_disable_postfix)
        return "";
    if (!m_comparison_val.empty() && m_comp_type != CompareType::NO_CT) {
        return " " + comparison_type_to_string(m_comp_type, !m_is_not) + " " + m_comparison_val;
    }
    std::string p = m_is_not ? m_inverted_postfix : m_standard_postfix;
    if (!p.empty() && p.front() != ' ')
        p.insert(p.begin(), ' ');
    return p;
}

std::string ToggleExpr::comparison_type_to_string(CompareType type, bool inverted)
{
    if (!inverted) {
        switch (type) {
        case CompareType::GT: return ">";
        case CompareType::LT: return "<";
        case CompareType::GTE: return ">=";
        case CompareType::LTE: return "<=";
        case CompareType::EQ: return "==";
        case CompareType::NEQ: return "!=";
        default: return "";
        }
    } else {
        switch (type) {
        case CompareType::GT: return "<=";
        case CompareType::LT: return ">=";
        case CompareType::GTE: return "<";
        case CompareType::LTE: return ">";
        case CompareType::EQ: return "!=";
        case CompareType::NEQ: return "==";
        default: return "";
        }
    }
}
ToggleExpr ToggleExpr::FromConfigBool(const DynamicPrintConfig* config, const std::string& opt_key, unsigned opt_idx)
{
    auto val = opt_idx == -1 ? config->opt_bool(opt_key) : config->opt_bool(opt_key, opt_idx);
    return {val, opt_key};
}

ToggleExpr ToggleExpr::FromConfigInt(
    const DynamicPrintConfig* config, const std::string& opt_key, CompareType comp_type, int comp_val, unsigned opt_idx)
{
    auto val = opt_idx == -1 ? config->opt_int(opt_key) : config->opt_int(opt_key, opt_idx);
    return ToggleExpr(compare(val, comp_type, comp_val), opt_key).set_comparison(comp_type, std::to_string(comp_val));
}

ToggleExprFragment<int> ToggleExpr::FromConfigInt(const DynamicPrintConfig* config, const std::string& opt_key, unsigned opt_idx)
{
    return {config, opt_key, opt_idx};
}

ToggleExpr ToggleExpr::FromConfigFloat(
    const DynamicPrintConfig* config, const std::string& opt_key, CompareType comp_type, double comp_val, unsigned opt_idx)
{
    auto val = opt_idx == -1 ? config->opt_float(opt_key) : config->opt_float(opt_key, opt_idx);
    return ToggleExpr(compare(val, comp_type, comp_val), opt_key).set_comparison(comp_type, std::to_string(comp_val));
}

ToggleExprFragment<double> ToggleExpr::FromConfigFloat(const DynamicPrintConfig* config, const std::string& opt_key, unsigned opt_idx)
{
    return {config, opt_key, opt_idx};
}

ToggleExpr ToggleExpr::FromConfigString(
    const DynamicPrintConfig* config, const std::string& opt_key, CompareType comp_type, const std::string& comp_val, unsigned opt_idx)
{
    auto val = opt_idx == -1 ? config->opt_string(opt_key) : config->opt_string(opt_key, opt_idx);
    return ToggleExpr(compare(val, comp_type, comp_val), opt_key).set_comparison(comp_type, comp_val);
}

ToggleExprFragment<std::string> ToggleExpr::FromConfigString(const DynamicPrintConfig* config, const std::string& opt_key, unsigned opt_idx)
{
    return {config, opt_key, opt_idx};
}

std::string ToggleExpr::build_reasons_string(std::string beginning_message, const std::vector<std::string>& reasons)
{
    if (reasons.empty()) return "";
    auto message = std::move(beginning_message);
    if (!message.empty()) {
        boost::trim(message);
        message += " ";
    }
    message += "Reasons:\n";

    for (auto& reason : reasons) {
        message += reason;
        message += "\n";
    }

    return message;
}
}} // namespace Slic3r::GUI
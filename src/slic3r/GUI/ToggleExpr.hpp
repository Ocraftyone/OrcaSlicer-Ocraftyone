
#ifndef ORCASLICER_TOGGLEEXPR_HPP
#define ORCASLICER_TOGGLEEXPR_HPP

namespace Slic3r::GUI {

enum class CompareType { NO_CT, GT, LT, GTE, LTE, EQ, NEQ };

class ToggleExpr
{
    bool m_value{};
    bool m_is_not{};

    // If the value indicates toggle off, these are the reasons why
    std::vector<std::string> m_reasons{};
    bool                     m_unformatted_reason{false};

    bool        m_has_prefixes{};
    std::string m_standard_prefix{};
    std::string m_inverted_prefix{};

    bool        m_disable_postfix{};
    std::string m_standard_postfix{" disabled"};
    std::string m_inverted_postfix{" enabled"};

    CompareType m_comp_type{CompareType::NO_CT};
    std::string m_comparison_val{};

    ToggleExpr(bool value, std::vector<std::string> reasons) : m_reasons(std::move(reasons)) {}

    static void get_reasons(std::vector<std::string>& reasons, const ToggleExpr& expr);

public:
    ToggleExpr(bool value, std::string name) : m_value(value)
    {
        if (!name.empty()) {
            m_unformatted_reason = true;
            m_reasons            = {std::move(name)};
        }
    }

    friend ToggleExpr operator&&(const ToggleExpr& lhs, const ToggleExpr& rhs)
    {
        if (lhs.m_value && rhs.m_value)
            return {true, ""};

        std::vector<std::string> reasons;
        if (!lhs.m_value)
            get_reasons(reasons, lhs);
        if (!rhs.m_value)
            get_reasons(reasons, rhs);

        return {false, reasons};
    }

    friend ToggleExpr operator||(const ToggleExpr& lhs, const ToggleExpr& rhs)
    {
        if (lhs.m_value || rhs.m_value)
            return {true, ""};

        std::vector<std::string> reasons;
        get_reasons(reasons, lhs);
        get_reasons(reasons, rhs);
        return {false, reasons};
    }

    friend ToggleExpr operator!(const ToggleExpr& rhs)
    {
        auto copy     = rhs;
        copy.m_value  = !copy.m_value;
        copy.m_is_not = !copy.m_is_not;
        return copy;
    }

    [[nodiscard]] std::string get_prefix() const;

    [[nodiscard]] std::string get_postfix() const;

    ///
    /// \param value_false_prefix When the provided value is false, what should the prefix be?
    /// \param opposite_prefix When the provided value is false when inverted ('!' operator), what should the prefix be?
    /// \return self
    ToggleExpr& set_prefixes(std::string value_false_prefix, std::string opposite_prefix)
    {
        m_standard_prefix = std::move(value_false_prefix);
        m_inverted_prefix = std::move(opposite_prefix);
        return *this;
    }

    ToggleExpr& disable_postfix()
    {
        m_disable_postfix = true;
        return *this;
    }

    ToggleExpr& set_postfixes(std::string value_false_postfix, std::string opposite_postfix)
    {
        m_standard_postfix = std::move(value_false_postfix);
        m_inverted_postfix = std::move(opposite_postfix);
        return *this;
    }

    /// Set the comparison that is occurring to generate the provided value
    /// \return self
    ToggleExpr& set_comparison(CompareType comp_type, std::string comparison_val)
    {
        m_comp_type      = comp_type;
        m_comparison_val = std::move(comparison_val);
        return *this;
    }

    [[nodiscard]] bool get_value() const { return m_value; }

    [[nodiscard]] std::vector<std::string> get_reasons() const
    {
        std::vector<std::string> out;
        get_reasons(out, *this);
        return out;
    }

    template<typename T> static bool compare(T value, CompareType comp_type, T comp_value);

    static std::string comparison_type_to_string(CompareType type, bool inverted);

    static ToggleExpr FromConfigBool(const DynamicPrintConfig* config, const std::string& opt_key, unsigned opt_idx = -1);

    static ToggleExpr FromConfigInt(
        const DynamicPrintConfig* config, const std::string& opt_key, CompareType comp_type, int comp_val, unsigned opt_idx = -1);

    static ToggleExpr FromConfigFloat(
        const DynamicPrintConfig* config, const std::string& opt_key, CompareType comp_type, double comp_val, unsigned opt_idx = -1);

    static std::string build_reasons_string(std::string beginning_message, const std::vector<std::string>& reasons);
};

} // namespace Slic3r::GUI

#endif // ORCASLICER_TOGGLEEXPR_HPP

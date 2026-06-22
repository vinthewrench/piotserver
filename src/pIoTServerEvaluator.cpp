//
//  pIoTServerEvaluator.cpp
//  pIoTServer
//
//  Created by vinnie on 1/16/25.
//

#include "pIoTServerEvaluator.hpp"
#include "LogMgr.hpp"

#include <cstdio>
#include <string>
#include <iostream>

using namespace std;

#define exprtk_disable_rtl_io
#define exprtk_disable_rtl_io_file
#define exprtk_disable_enhanced_features
#define exprtk_disable_rtl_vecops
#define exprtk_disable_caseinsensitivity

#include "exprtk.hpp"

/**
 * Celsius to Fahrenheit converter for ExprTk.
 *
 * Usage in expressions:
 *
 *   F(GREENHOUSE)
 *   c_to_f(GREENHOUSE)
 */
template <typename T>
struct c_to_f_function : public exprtk::ifunction<T> {

    c_to_f_function()
    : exprtk::ifunction<T>(1) {
    }

    inline T operator()(const T& c) override {
        return (c * T(9) / T(5)) + T(32);
    }
};

/**
 * Fahrenheit to Celsius converter for ExprTk.
 *
 * Usage in expressions:
 *
 *   C(81)
 *   f_to_c(81)
 */
template <typename T>
struct f_to_c_function : public exprtk::ifunction<T> {

    f_to_c_function()
    : exprtk::ifunction<T>(1) {
    }

    inline T operator()(const T& f) override {
        return (f - T(32)) * T(5) / T(9);
    }
};

bool evaluateExpression(string expr,
                        vector<pIoTServerDB::numericValueSnapshot_t> &vars,
                        double &result) {

    bool status = false;

    using T = double;

    typedef exprtk::symbol_table<T> symbol_table_t;
    typedef exprtk::expression<T>   expression_t;
    typedef exprtk::parser<T>       parser_t;

    symbol_table_t symbol_table;

    /*
     * Register pIoTServer numeric values as variables.
     *
     * ExprTk binds variables by reference, so mirrorValue must remain valid
     * until after expression.value() completes.
     */
    for(auto &s : vars) {
        s.mirrorValue = s.value;
        symbol_table.add_variable(s.name, s.mirrorValue);
    }

    /*
     * Temperature conversion helper functions.
     *
     * These are intentionally small and generic so rules can be written in
     * human-friendly units while DB/device values remain stored in their
     * native units.
     *
     *   F(x)      Celsius -> Fahrenheit
     *   C(x)      Fahrenheit -> Celsius
     *   c_to_f(x) Celsius -> Fahrenheit
     *   f_to_c(x) Fahrenheit -> Celsius
     */
    c_to_f_function<T> c_to_f;
    f_to_c_function<T> f_to_c;

    symbol_table.add_function("F", c_to_f);
    symbol_table.add_function("C", f_to_c);
    symbol_table.add_function("c_to_f", c_to_f);
    symbol_table.add_function("f_to_c", f_to_c);

    symbol_table.add_constants();

    expression_t expression;
    expression.register_symbol_table(symbol_table);

    parser_t parser;

    if(parser.compile(expr, expression)) {
        result = expression.value();
        status = true;
    }
    else {
        LOGT_ERROR("EXPRTK compile failed: %s", expr.c_str());

        for(size_t i = 0; i < parser.error_count(); ++i) {
            typedef exprtk::parser_error::type error_t;

            error_t error = parser.get_error(i);

            LOGT_ERROR("EXPRTK error %zu: position=%zu type=%s diagnostic=%s",
                       i,
                       error.token.position,
                       exprtk::parser_error::to_str(error.mode).c_str(),
                       error.diagnostic.c_str());
        }
    }

    return status;
}

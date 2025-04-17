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

bool evaluateExpression(string expr, vector<pIoTServerDB::numericValueSnapshot_t> &vars, double &result){
    
    bool status = false;
    
    using T              = double;
    
    typedef exprtk::symbol_table<T> symbol_table_t;
    typedef exprtk::expression<T>   expression_t;
    typedef exprtk::parser<T>       parser_t;
    
    symbol_table_t symbol_table;
    
    for(auto &s : vars){
        s.mirrorValue = s.value;
        symbol_table.add_variable(s.name , s.value );
    }

    expression_t expression;
    expression.register_symbol_table(symbol_table);
    
    parser_t parser;
    typedef typename parser_t::dependent_entity_collector::symbol_t symbol_t;
    std::deque<symbol_t> variable_list;
     
    if (!parser.compile(expr, expression))  {
        
        LOGT_ERROR("Error: %s  Expression: %s\n",
                   parser.error().c_str(),
                   expr.c_str());
        return false;
    }
 
    auto res =  expression.value();
    if (!isnan(res)) {
        result = res;
        status = true;
    }
    else if(parser.dec().return_present()){
        if (expression.return_invoked()){
            
            typedef exprtk::results_context<T> results_context_t;
            const results_context_t& results = expression.results();
            results.get_scalar(0, result);

//#warning DEBUG  cout << "return "  << result  << endl;
            status =  true;
        }
    }
 
    if(status){
        for(auto &e: vars)
             if(!e.readOnly && (e.value != e.mirrorValue))
                e.wasUpdated = true;
    }
    
    return status;
}

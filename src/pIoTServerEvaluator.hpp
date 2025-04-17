//
//  pIoTServerEvaluator.hpp
//  pIoTServer
//
//  Created by vinnie on 1/16/25.
//

#ifndef pIoTServerEvaluator_hpp
#define pIoTServerEvaluator_hpp

#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <map>

#include "pIoTServerDB.hpp"   // for numericValueSnapshot_t

   
bool evaluateExpression(string expr, vector<pIoTServerDB::numericValueSnapshot_t> &vars, double &result);
    
 
#endif /* pIoTServerEvaluator_hpp */

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

package org.apache.doris.nereids.trees.expressions.functions.scalar;

import org.apache.doris.catalog.FunctionSignature;
import org.apache.doris.nereids.trees.expressions.Expression;
import org.apache.doris.nereids.trees.expressions.functions.AlwaysNotNullable;
import org.apache.doris.nereids.trees.expressions.functions.ExplicitlyCastableSignature;
import org.apache.doris.nereids.trees.expressions.functions.ExpressionTrait;
import org.apache.doris.nereids.trees.expressions.visitor.ExpressionVisitor;
import org.apache.doris.nereids.types.DataType;
import org.apache.doris.nereids.types.StructField;
import org.apache.doris.nereids.types.StructType;

import com.google.common.collect.ImmutableList;

import java.util.List;

/**
 * ScalarFunction 'struct'.
 */
public class CreateStruct extends ScalarFunction
        implements ExplicitlyCastableSignature, AlwaysNotNullable {

    public static final List<FunctionSignature> SIGNATURES = ImmutableList.of(
            FunctionSignature.ret(StructType.SYSTEM_DEFAULT).args()
    );

    /**
     * constructor with 0 or more arguments.
     */
    public CreateStruct(Expression... varArgs) {
        super("struct", varArgs);
    }

    /**
     * withChildren.
     */
    @Override
    public CreateStruct withChildren(List<Expression> children) {
        return new CreateStruct(children.toArray(new Expression[0]));
    }

    @Override
    public <R, C> R accept(ExpressionVisitor<R, C> visitor, C context) {
        return visitor.visitCreateStruct(this, context);
    }

    @Override
    public List<FunctionSignature> getSignatures() {
        if (arity() == 0) {
            return SIGNATURES;
        } else {
            ImmutableList.Builder<StructField> structFields = ImmutableList.builder();
            for (int i = 0; i < arity(); i++) {
                structFields.add(new StructField(String.valueOf(i + 1), children.get(i).getDataType(), true, ""));
            }
            return ImmutableList.of(FunctionSignature.ret(new StructType(structFields.build()))
                    .args(children.stream().map(ExpressionTrait::getDataType).toArray(DataType[]::new)));
        }
    }
}

/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @date 2017
 * Converts the AST into json format
 */

#include <libsolidity/ast/ASTJsonConverter.h>

#include <libsolidity/ast/AST.h>

#include <libsolutil/JSON.h>
#include <libsolutil/UTF8.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <vector>
#include <algorithm>

using namespace std;
using namespace solidity::langutil;

namespace solidity::frontend
{

ASTJsonConverter::ASTJsonConverter(bool _legacy, map<string, unsigned> _sourceIndices):
	m_legacy(_legacy),
	m_sourceIndices(_sourceIndices)
{
}


void ASTJsonConverter::setJsonNode(
	ASTNode const& _node,
	string const& _nodeName,
	initializer_list<pair<string, Json::Value>>&& _attributes
)
{
	ASTJsonConverter::setJsonNode(
		_node,
		_nodeName,
		std::vector<pair<string, Json::Value>>(std::move(_attributes))
	);
}

void ASTJsonConverter::setJsonNode(
	ASTNode const& _node,
	string const& _nodeType,
	std::vector<pair<string, Json::Value>>&& _attributes
)
{
	m_currentValue = Json::objectValue;
	m_currentValue["id"] = nodeId(_node);
	m_currentValue["src"] = sourceLocationToString(_node.location());
	if (!m_legacy)
	{
		m_currentValue["nodeType"] = _nodeType;
		for (auto& e: _attributes)
			m_currentValue[e.first] = std::move(e.second);
	}
	else
	{
		m_currentValue["name"] = _nodeType;
		Json::Value attrs(Json::objectValue);
		if (
			//these nodeTypes need to have a children-node even if it is empty
			(_nodeType == "VariableDeclaration") ||
			(_nodeType == "ParameterList") ||
			(_nodeType == "Block") ||
			(_nodeType == "InlineAssembly") ||
			(_nodeType == "Throw")
		)
			m_currentValue["children"] = Json::arrayValue;

		for (auto& e: _attributes)
		{
			if ((!e.second.isNull()) && (
				(e.second.isObject() && e.second.isMember("name")) ||
				(e.second.isArray() && e.second[0].isObject() && e.second[0].isMember("name")) ||
				(e.first == "declarations") // (in the case (_,x)= ... there's a nullpointer at [0]
			))
			{
				if (e.second.isObject())
				{
					if (!m_currentValue["children"].isArray())
						m_currentValue["children"] = Json::arrayValue;
					appendMove(m_currentValue["children"], std::move(e.second));
				}
				if (e.second.isArray())
					for (auto& child: e.second)
						if (!child.isNull())
						{
							if (!m_currentValue["children"].isArray())
								m_currentValue["children"] = Json::arrayValue;
							appendMove(m_currentValue["children"], std::move(child));
						}
			}
			else
			{
				if (e.first == "typeDescriptions")
					attrs["type"] = Json::Value(e.second["typeString"]);
				else
					attrs[e.first] = std::move(e.second);
			}
		}
		if (!attrs.empty())
			m_currentValue["attributes"] = std::move(attrs);
	}
}

size_t ASTJsonConverter::sourceIndexFromLocation(SourceLocation const& _location) const
{
	if (_location.source && m_sourceIndices.count(_location.source->name()))
		return m_sourceIndices.at(_location.source->name());
	else
		return size_t(-1);
}

string ASTJsonConverter::sourceLocationToString(SourceLocation const& _location) const
{
	size_t sourceIndex = sourceIndexFromLocation(_location);
	int length = -1;
	if (_location.start >= 0 && _location.end >= 0)
		length = _location.end - _location.start;
	return to_string(_location.start) + ":" + to_string(length) + ":" + to_string(sourceIndex);
}

string ASTJsonConverter::namePathToString(std::vector<ASTString> const& _namePath)
{
	return boost::algorithm::join(_namePath, ".");
}

Json::Value ASTJsonConverter::typePointerToJson(TypePointer _tp, bool _short)
{
	Json::Value typeDescriptions(Json::objectValue);
	typeDescriptions["typeString"] = _tp ? Json::Value(_tp->toString(_short)) : Json::nullValue;
	typeDescriptions["typeIdentifier"] = _tp ? Json::Value(_tp->identifier()) : Json::nullValue;
	return typeDescriptions;

}
Json::Value ASTJsonConverter::typePointerToJson(std::optional<FuncCallArguments> const& _tps)
{
	if (_tps)
	{
		Json::Value arguments(Json::arrayValue);
		for (auto const& tp: _tps->types)
			appendMove(arguments, typePointerToJson(tp));
		return arguments;
	}
	else
		return Json::nullValue;
}

void ASTJsonConverter::appendExpressionAttributes(
	std::vector<pair<string, Json::Value>>& _attributes,
	ExpressionAnnotation const& _annotation
)
{
	std::vector<pair<string, Json::Value>> exprAttributes = {
		make_pair("typeDescriptions", typePointerToJson(_annotation.type)),
		make_pair("isConstant", _annotation.isConstant),
		make_pair("isPure", _annotation.isPure),
		make_pair("isLValue", _annotation.isLValue),
		make_pair("lValueRequested", _annotation.lValueRequested),
		make_pair("argumentTypes", typePointerToJson(_annotation.arguments))
	};
	_attributes += exprAttributes;
}

void ASTJsonConverter::print(ostream& _stream, ASTNode const& _node, const std::string& ident)
{
	std::stringstream ss(util::jsonPrettyPrint(toJson(_node)));
	std::string line;
	while (std::getline(ss, line)) {
		_stream << ident << line << endl;
	}
}

Json::Value&& ASTJsonConverter::toJson(ASTNode const& _node)
{
	_node.accept(*this);
	return std::move(m_currentValue);
}

bool ASTJsonConverter::visit(SourceUnit const& _node)
{
	Json::Value exportedSymbols = Json::objectValue;
	for (auto const& sym: _node.annotation().exportedSymbols)
	{
		exportedSymbols[sym.first] = Json::arrayValue;
		for (Declaration const* overload: sym.second)
			exportedSymbols[sym.first].append(nodeId(*overload));
	}
	setJsonNode(
		_node,
		"SourceUnit",
		{
			make_pair("absolutePath", _node.annotation().path),
			make_pair("exportedSymbols", move(exportedSymbols)),
			make_pair("nodes", toJson(_node.nodes()))
		}
	);
	return false;
}

bool ASTJsonConverter::visit(PragmaDirective const& _node)
{
	Json::Value literals(Json::arrayValue);
	for (auto const& literal: _node.literals())
		literals.append(literal);
	setJsonNode( _node, "PragmaDirective", {
		make_pair("literals", std::move(literals))
	});
	return false;
}

bool ASTJsonConverter::visit(ImportDirective const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("file", _node.path()),
		make_pair("absolutePath", _node.annotation().absolutePath),
		make_pair(m_legacy ? "SourceUnit" : "sourceUnit", nodeId(*_node.annotation().sourceUnit)),
		make_pair("scope", idOrNull(_node.scope()))
	};
	attributes.emplace_back("unitAlias", _node.name());
	Json::Value symbolAliases(Json::arrayValue);
	for (auto const& symbolAlias: _node.symbolAliases())
	{
		Json::Value tuple(Json::objectValue);
		solAssert(symbolAlias.symbol, "");
		tuple["foreign"] = toJson(*symbolAlias.symbol);
		tuple["local"] =  symbolAlias.alias ? Json::Value(*symbolAlias.alias) : Json::nullValue;
		symbolAliases.append(tuple);
	}
	attributes.emplace_back("symbolAliases", std::move(symbolAliases));
	setJsonNode(_node, "ImportDirective", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(ContractDefinition const& _node)
{
	setJsonNode(_node, "ContractDefinition", {
		make_pair("name", _node.name()),
		make_pair("documentation", _node.documentation() ? toJson(*_node.documentation()) : Json::nullValue),
		make_pair("contractKind", contractKind(_node.contractKind())),
		make_pair("abstract", _node.abstract()),
		make_pair("fullyImplemented", _node.annotation().unimplementedFunctions.empty()),
		make_pair("linearizedBaseContracts", getContainerIds(_node.annotation().linearizedBaseContracts)),
		make_pair("baseContracts", toJson(_node.baseContracts())),
		make_pair("contractDependencies", getContainerIds(_node.annotation().contractDependencies, true)),
		make_pair("nodes", toJson(_node.subNodes())),
		make_pair("scope", idOrNull(_node.scope()))
	});
	return false;
}

bool ASTJsonConverter::visit(InheritanceSpecifier const& _node)
{
	setJsonNode(_node, "InheritanceSpecifier", {
		make_pair("baseName", toJson(_node.name())),
		make_pair("arguments", _node.arguments() ? toJson(*_node.arguments()) : Json::nullValue)
	});
	return false;
}

bool ASTJsonConverter::visit(UsingForDirective const& _node)
{
	setJsonNode(_node, "UsingForDirective", {
		make_pair("libraryName", toJson(_node.libraryName())),
		make_pair("typeName", _node.typeName() ? toJson(*_node.typeName()) : Json::nullValue)
	});
	return false;
}

bool ASTJsonConverter::visit(StructDefinition const& _node)
{
	setJsonNode(_node, "StructDefinition", {
		make_pair("name", _node.name()),
		make_pair("visibility", Declaration::visibilityToString(_node.visibility())),
		make_pair("canonicalName", _node.annotation().canonicalName),
		make_pair("members", toJson(_node.members())),
		make_pair("scope", idOrNull(_node.scope()))
	});
	return false;
}

bool ASTJsonConverter::visit(EnumDefinition const& _node)
{
	setJsonNode(_node, "EnumDefinition", {
		make_pair("name", _node.name()),
		make_pair("canonicalName", _node.annotation().canonicalName),
		make_pair("members", toJson(_node.members()))
	});
	return false;
}

bool ASTJsonConverter::visit(EnumValue const& _node)
{
	setJsonNode(_node, "EnumValue", {
		make_pair("name", _node.name())
	});
	return false;
}

bool ASTJsonConverter::visit(ParameterList const& _node)
{
	setJsonNode(_node, "ParameterList", {
		make_pair("parameters", toJson(_node.parameters()))
	});
	return false;
}

bool ASTJsonConverter::visit(OverrideSpecifier const& _node)
{
	setJsonNode(_node, "OverrideSpecifier", {
		make_pair("overrides", toJson(_node.overrides()))
	});
	return false;
}

bool ASTJsonConverter::visit(FunctionDefinition const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("name", _node.name()),
		make_pair("documentation", _node.documentation() ? toJson(*_node.documentation()) : Json::nullValue),
		make_pair("kind", TokenTraits::toString(_node.kind())),
		make_pair("stateMutability", stateMutabilityToString(_node.stateMutability())),
		make_pair("visibility", Declaration::visibilityToString(_node.visibility())),
		make_pair("virtual", _node.markedVirtual()),
		make_pair("overrides", _node.overrides() ? toJson(*_node.overrides()) : Json::nullValue),
		make_pair("parameters", toJson(_node.parameterList())),
		make_pair("returnParameters", toJson(*_node.returnParameterList())),
		make_pair("modifiers", toJson(_node.modifiers())),
		make_pair("body", _node.isImplemented() ? toJson(_node.body()) : Json::nullValue),
		make_pair("implemented", _node.isImplemented()),
		make_pair("scope", idOrNull(_node.scope()))
	};
//	if (_node.isPartOfExternalInterface())
//		attributes.emplace_back("functionSelector", _node.externalIdentifierHex());
	if (!_node.annotation().baseFunctions.empty())
		attributes.emplace_back(make_pair("baseFunctions", getContainerIds(_node.annotation().baseFunctions, true)));
	if (m_legacy)
		attributes.emplace_back("isConstructor", _node.isConstructor());
	setJsonNode(_node, "FunctionDefinition", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(VariableDeclaration const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("name", _node.name()),
		make_pair("typeName", toJsonOrNull(_node.typeName())),
		make_pair("constant", _node.isConstant()),
		make_pair("stateVariable", _node.isStateVariable()),
		make_pair("overrides", _node.overrides() ? toJson(*_node.overrides()) : Json::nullValue),
		make_pair("visibility", Declaration::visibilityToString(_node.visibility())),
		make_pair("value", _node.value() ? toJson(*_node.value()) : Json::nullValue),
		make_pair("scope", idOrNull(_node.scope())),
		make_pair("typeDescriptions", typePointerToJson(_node.annotation().type, true))
	};
	if (_node.isStateVariable() && _node.isPublic())
		attributes.emplace_back("functionSelector", _node.externalIdentifierHex());
	if (m_inEvent)
		attributes.emplace_back("indexed", _node.isIndexed());
	if (!_node.annotation().baseFunctions.empty())
		attributes.emplace_back(make_pair("baseFunctions", getContainerIds(_node.annotation().baseFunctions, true)));
	setJsonNode(_node, "VariableDeclaration", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(ModifierDefinition const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("name", _node.name()),
		make_pair("documentation", _node.documentation() ? toJson(*_node.documentation()) : Json::nullValue),
		make_pair("visibility", Declaration::visibilityToString(_node.visibility())),
		make_pair("parameters", toJson(_node.parameterList())),
		make_pair("virtual", _node.markedVirtual()),
		make_pair("overrides", _node.overrides() ? toJson(*_node.overrides()) : Json::nullValue),
		make_pair("body", toJson(_node.body()))
	};
	if (!_node.annotation().baseFunctions.empty())
		attributes.emplace_back(make_pair("baseModifiers", getContainerIds(_node.annotation().baseFunctions, true)));
	setJsonNode(_node, "ModifierDefinition", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(ModifierInvocation const& _node)
{
	setJsonNode(_node, "ModifierInvocation", {
		make_pair("modifierName", toJson(*_node.name())),
		make_pair("arguments", _node.arguments() ? toJson(*_node.arguments()) : Json::nullValue)
	});
	return false;
}

bool ASTJsonConverter::visit(EventDefinition const& _node)
{
	m_inEvent = true;
	setJsonNode(_node, "EventDefinition", {
		make_pair("name", _node.name()),
		make_pair("documentation", _node.documentation() ? toJson(*_node.documentation()) : Json::nullValue),
		make_pair("parameters", toJson(_node.parameterList())),
		make_pair("anonymous", _node.isAnonymous())
	});
	return false;
}

bool ASTJsonConverter::visit(ElementaryTypeName const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
			make_pair("name", _node.typeName().toString()),
			make_pair("typeDescriptions", typePointerToJson(_node.annotation().type, true))
	};
	setJsonNode(_node, "ElementaryTypeName", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(UserDefinedTypeName const& _node)
{
	setJsonNode(_node, "UserDefinedTypeName", {
		make_pair("name", namePathToString(_node.namePath())),
		make_pair("referencedDeclaration", idOrNull(_node.annotation().referencedDeclaration)),
		make_pair("contractScope", idOrNull(_node.annotation().contractScope)),
		make_pair("typeDescriptions", typePointerToJson(_node.annotation().type, true))
	});
	return false;
}

bool ASTJsonConverter::visit(FunctionTypeName const& _node)
{
	setJsonNode(_node, "FunctionTypeName", {
		make_pair("visibility", Declaration::visibilityToString(_node.visibility())),
		make_pair("stateMutability", stateMutabilityToString(_node.stateMutability())),
		make_pair("parameterTypes", toJson(*_node.parameterTypeList())),
		make_pair("returnParameterTypes", toJson(*_node.returnParameterTypeList())),
		make_pair("typeDescriptions", typePointerToJson(_node.annotation().type, true))
	});
	return false;
}

bool ASTJsonConverter::visit(Mapping const& _node)
{
	setJsonNode(_node, "Mapping", {
		make_pair("keyType", toJson(_node.keyType())),
		make_pair("valueType", toJson(_node.valueType())),
		make_pair("typeDescriptions", typePointerToJson(_node.annotation().type, true))
	});
	return false;
}

bool ASTJsonConverter::visit(Optional const& _node) {
	setJsonNode(_node, "Optional", {
		make_pair("maybeTypes", toJson(_node.maybeTypes())),
	});
	return false;
}

bool ASTJsonConverter::visit(TvmVector const& _node) {
	setJsonNode(_node, "vector", {
		make_pair("type", toJson(_node.type())),
	});
	return false;
}

bool ASTJsonConverter::visit(ArrayTypeName const& _node)
{
	setJsonNode(_node, "ArrayTypeName", {
		make_pair("baseType", toJson(_node.baseType())),
		make_pair("length", toJsonOrNull(_node.length())),
		make_pair("typeDescriptions", typePointerToJson(_node.annotation().type, true))
	});
	return false;
}

bool ASTJsonConverter::visit(InlineAssembly const& /*_node*/)
{
	return false;
}

bool ASTJsonConverter::visit(Block const& _node)
{
	setJsonNode(_node, "Block", {
		make_pair("statements", toJson(_node.statements()))
	});
	return false;
}

bool ASTJsonConverter::visit(PlaceholderStatement const& _node)
{
	setJsonNode(_node, "PlaceholderStatement", {});
	return false;
}

bool ASTJsonConverter::visit(IfStatement const& _node)
{
	setJsonNode(_node, "IfStatement", {
		make_pair("condition", toJson(_node.condition())),
		make_pair("trueBody", toJson(_node.trueStatement())),
		make_pair("falseBody", toJsonOrNull(_node.falseStatement()))
	});
	return false;
}

bool ASTJsonConverter::visit(TryCatchClause const& _node)
{
	setJsonNode(_node, "TryCatchClause", {
		make_pair("errorName", _node.errorName()),
		make_pair("parameters", toJsonOrNull(_node.parameters())),
		make_pair("block", toJson(_node.block()))
	});
	return false;
}

bool ASTJsonConverter::visit(TryStatement const& _node)
{
	setJsonNode(_node, "TryStatement", {
		make_pair("externalCall", toJson(_node.externalCall())),
		make_pair("clauses", toJson(_node.clauses()))
	});
	return false;
}

bool ASTJsonConverter::visit(WhileStatement const& _node)
{
	std::string name;
	switch (_node.loopType()) {
		case WhileStatement::LoopType::DO_WHILE:
			name = "DoWhileStatement";
			break;
		case WhileStatement::LoopType::WHILE_DO:
			name = "WhileStatement";
			break;
		case WhileStatement::LoopType::REPEAT:
			name = "RepeatStatement";
			break;
	}
	setJsonNode(
		_node,
		name,
		{
			make_pair("condition", toJson(_node.condition())),
			make_pair("body", toJson(_node.body()))
		}
	);
	return false;
}

bool ASTJsonConverter::visit(ForStatement const& _node)
{
	setJsonNode(_node, "ForStatement", {
		make_pair("initializationExpression", toJsonOrNull(_node.initializationExpression())),
		make_pair("condition", toJsonOrNull(_node.condition())),
		make_pair("loopExpression", toJsonOrNull(_node.loopExpression())),
		make_pair("body", toJson(_node.body()))
	});
	return false;
}

bool ASTJsonConverter::visit(ForEachStatement const& _node) {
	setJsonNode(_node, "ForEachStatement", {
		make_pair("rangeDeclaration", toJsonOrNull(_node.rangeDeclaration())),
		make_pair("rangeExpression", toJsonOrNull(_node.rangeExpression())),
		make_pair("body", toJson(_node.body()))
	});
	return false;
}

bool ASTJsonConverter::visit(Continue const& _node)
{
	setJsonNode(_node, "Continue", {});
	return false;
}

bool ASTJsonConverter::visit(Break const& _node)
{
	setJsonNode(_node, "Break", {});
	return false;
}

bool ASTJsonConverter::visit(Return const& _node)
{
	setJsonNode(_node, "Return", {
		make_pair("expression", toJsonOrNull(_node.expression())),
		make_pair("functionReturnParameters", idOrNull(_node.annotation().functionReturnParameters))
	});
	return false;
}

bool ASTJsonConverter::visit(Throw const& _node)
{
	setJsonNode(_node, "Throw", {});
	return false;
}

bool ASTJsonConverter::visit(EmitStatement const& _node)
{
	Json::Value names(Json::arrayValue);
	for (auto const& name: _node.names())
		names.append(Json::Value(*name));

	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("names", std::move(names)),
		make_pair("options", toJson(_node.options())),
		make_pair("eventCall", toJson(_node.eventCall()))
	};

	setJsonNode(_node, "EmitStatement", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(VariableDeclarationStatement const& _node)
{
	Json::Value varDecs(Json::arrayValue);
	for (auto const& v: _node.declarations())
		appendMove(varDecs, idOrNull(v.get()));
	setJsonNode(_node, "VariableDeclarationStatement", {
		make_pair("assignments", std::move(varDecs)),
		make_pair("declarations", toJson(_node.declarations())),
		make_pair("initialValue", toJsonOrNull(_node.initialValue()))
	});
	return false;
}

bool ASTJsonConverter::visit(ExpressionStatement const& _node)
{
	setJsonNode(_node, "ExpressionStatement", {
		make_pair("expression", toJson(_node.expression()))
	});
	return false;
}

bool ASTJsonConverter::visit(Conditional const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("condition", toJson(_node.condition())),
		make_pair("trueExpression", toJson(_node.trueExpression())),
		make_pair("falseExpression", toJson(_node.falseExpression()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "Conditional", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(Assignment const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("operator", TokenTraits::toString(_node.assignmentOperator())),
		make_pair("leftHandSide", toJson(_node.leftHandSide())),
		make_pair("rightHandSide", toJson(_node.rightHandSide()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode( _node, "Assignment", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(TupleExpression const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("isInlineArray", Json::Value(_node.isInlineArray())),
		make_pair("components", toJson(_node.components())),
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "TupleExpression", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(UnaryOperation const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("prefix", _node.isPrefixOperation()),
		make_pair("operator", TokenTraits::toString(_node.getOperator())),
		make_pair("subExpression", toJson(_node.subExpression()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "UnaryOperation", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(BinaryOperation const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("operator", TokenTraits::toString(_node.getOperator())),
		make_pair("leftExpression", toJson(_node.leftExpression())),
		make_pair("rightExpression", toJson(_node.rightExpression())),
		make_pair("commonType", typePointerToJson(_node.annotation().commonType)),
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "BinaryOperation", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(FunctionCall const& _node)
{
	Json::Value names(Json::arrayValue);
	for (auto const& name: _node.names())
		names.append(Json::Value(*name));
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("expression", toJson(_node.expression())),
		make_pair("names", std::move(names)),
		make_pair("arguments", toJson(_node.arguments())),
		make_pair("tryCall", _node.annotation().tryCall)
	};
	if (m_legacy)
	{
		attributes.emplace_back("isStructConstructorCall", _node.annotation().kind == FunctionCallKind::StructConstructorCall);
		attributes.emplace_back("type_conversion", _node.annotation().kind == FunctionCallKind::TypeConversion);
	}
	else
		attributes.emplace_back("kind", functionCallKind(_node.annotation().kind));
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "FunctionCall", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(FunctionCallOptions const& _node)
{
	Json::Value names(Json::arrayValue);
	for (auto const& name: _node.names())
		names.append(Json::Value(*name));

	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("expression", toJson(_node.expression())),
		make_pair("names", std::move(names)),
		make_pair("options", toJson(_node.options())),
	};
	appendExpressionAttributes(attributes, _node.annotation());

	setJsonNode(_node, "FunctionCallOptions", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(InitializerList const& _node)
{
	Json::Value names(Json::arrayValue);
	for (auto const& name: _node.names())
		names.append(Json::Value(*name));

	std::vector<pair<string, Json::Value>> attributes = {
			make_pair("names", std::move(names)),
			make_pair("options", toJson(_node.options())),
	};
	appendExpressionAttributes(attributes, _node.annotation());

	setJsonNode(_node, "InitializerList", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(CallList const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("expression", toJson(*_node.function())),
		make_pair("arguments", toJson(_node.arguments())),
	};
	appendExpressionAttributes(attributes, _node.annotation());

	setJsonNode(_node, "CallList", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(NewExpression const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("typeName", toJson(_node.typeName()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "NewExpression", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(MemberAccess const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair(m_legacy ? "member_name" : "memberName", _node.memberName()),
		make_pair("expression", toJson(_node.expression())),
		make_pair("referencedDeclaration", idOrNull(_node.annotation().referencedDeclaration)),
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "MemberAccess", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(IndexAccess const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("baseExpression", toJson(_node.baseExpression())),
		make_pair("indexExpression", toJsonOrNull(_node.indexExpression())),
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "IndexAccess", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(IndexRangeAccess const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("baseExpression", toJson(_node.baseExpression())),
		make_pair("startExpression", toJsonOrNull(_node.startExpression())),
		make_pair("endExpression", toJsonOrNull(_node.endExpression())),
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "IndexRangeAccess", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(Identifier const& _node)
{
	Json::Value overloads(Json::arrayValue);
	for (auto const& dec: _node.annotation().overloadedDeclarations)
		overloads.append(nodeId(*dec));
	setJsonNode(_node, "Identifier", {
		make_pair(m_legacy ? "value" : "name", _node.name()),
		make_pair("referencedDeclaration", idOrNull(_node.annotation().referencedDeclaration)),
		make_pair("overloadedDeclarations", overloads),
		make_pair("typeDescriptions", typePointerToJson(_node.annotation().type)),
		make_pair("argumentTypes", typePointerToJson(_node.annotation().arguments))
	});
	return false;
}

bool ASTJsonConverter::visit(ElementaryTypeNameExpression const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair(m_legacy ? "value" : "typeName", toJson(_node.type()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "ElementaryTypeNameExpression", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(MappingNameExpression const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("typeName", toJson(_node.type()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "MappingNameExpression", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(OptionalNameExpression const& _node)
{
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("typeName", toJson(_node.type()))
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "OptionalNameExpression", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(Literal const& _node)
{
	Json::Value value{_node.value()};
	if (!util::validateUTF8(_node.value()))
		value = Json::nullValue;
	Token subdenomination = Token(_node.subDenomination());
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair(m_legacy ? "token" : "kind", literalTokenKind(_node.token())),
		make_pair("value", value),
		make_pair(m_legacy ? "hexvalue" : "hexValue", util::toHex(util::asBytes(_node.value()))),
		make_pair(
			"subdenomination",
			subdenomination == Token::Illegal ?
			Json::nullValue :
			Json::Value{TokenTraits::toString(subdenomination)}
		)
	};
	appendExpressionAttributes(attributes, _node.annotation());
	setJsonNode(_node, "Literal", std::move(attributes));
	return false;
}

bool ASTJsonConverter::visit(StructuredDocumentation const& _node)
{
	Json::Value text{*_node.text()};
	std::vector<pair<string, Json::Value>> attributes = {
		make_pair("text", text)
	};
	setJsonNode(_node, "StructuredDocumentation", std::move(attributes));
	return false;
}



void ASTJsonConverter::endVisit(EventDefinition const&)
{
	m_inEvent = false;
}

string ASTJsonConverter::contractKind(ContractKind _kind)
{
	switch (_kind)
	{
	case ContractKind::Interface:
		return "interface";
	case ContractKind::Contract:
		return "contract";
	case ContractKind::Library:
		return "library";
	}

	// To make the compiler happy
	return {};
}

string ASTJsonConverter::functionCallKind(FunctionCallKind _kind)
{
	switch (_kind)
	{
	case FunctionCallKind::FunctionCall:
		return "functionCall";
	case FunctionCallKind::TypeConversion:
		return "typeConversion";
	case FunctionCallKind::StructConstructorCall:
		return "structConstructorCall";
	default:
		solAssert(false, "Unknown kind of function call.");
	}
}

string ASTJsonConverter::literalTokenKind(Token _token)
{
	switch (_token)
	{
	case Token::Number:
		return "number";
	case Token::StringLiteral:
	case Token::HexStringLiteral:
		return "string";
	case Token::TrueLiteral:
	case Token::FalseLiteral:
		return "bool";
	case Token::EmptyMap:
		return "mapping";
	case Token::NullLiteral:
		return "optional";
	default:
		solAssert(false, "Unknown kind of literal token.");
	}
}

string ASTJsonConverter::type(Expression const& _expression)
{
	return _expression.annotation().type ? _expression.annotation().type->toString() : "Unknown";
}

string ASTJsonConverter::type(VariableDeclaration const& _varDecl)
{
	return _varDecl.annotation().type ? _varDecl.annotation().type->toString() : "Unknown";
}

}

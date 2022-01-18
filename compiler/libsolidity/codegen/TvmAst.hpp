/*
 * Copyright 2018-2021 TON DEV SOLUTIONS LTD.
 *
 * Licensed under the  terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License.
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the  GNU General Public License for more details at: https://www.gnu.org/licenses/gpl-3.0.html
 */
/**
 * @author TON Labs <connect@tonlabs.io>
 * @date 2021
 * TVM Solidity abstract syntax tree.
 */

#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <liblangutil/Exceptions.h>

#include <boost/noncopyable.hpp>

template <class T>
using Pointer = std::shared_ptr<T>;

namespace solidity::frontend
{
	class TvmAstVisitor;

	template <class NodeType, typename... Args>
	Pointer<NodeType> createNode(Args&& ... _args)
	{
		return std::make_shared<NodeType>(std::forward<Args>(_args)...);
	}

	class TvmAstNode : private boost::noncopyable, public std::enable_shared_from_this<TvmAstNode> {
	public:
		virtual ~TvmAstNode() = default;
		virtual void accept(TvmAstVisitor& _visitor) = 0;
		virtual bool operator==(TvmAstNode const& _node) const = 0;
	};

	class Inst : public TvmAstNode {
	};

	class Loc : public Inst {
	public:
		explicit Loc(std::string  _file, int _line) : m_file{std::move(_file)}, m_line{_line} { }
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const& n) const override;
		std::string const& file() const { return m_file; }
		int line() const { return m_line; }
	private:
		std::string m_file;
		int m_line;
	};

	class Stack : public Inst {
	public:
		enum class Opcode {
			DROP,
			BLKDROP2, // BLKDROP2 1, 1
			POP_S,    // POP_S 1

			BLKPUSH, // BLKPUSH 1, i | BLKPUSH 3, i         |  BLKPUSH 2, 1 (DUP2)  |  BLKPUSH 3, 1 (OVER2)
			PUSH2_S, //              |                      |  PUSH2 S1, S0         |  PUSH2 S3, S2
			PUSH3_S, //              | PUSH3 Si, Si-1, Si-2 |                       |
			PUSH_S,  // PUSH Si      |                      |                       |

			BLKSWAP, //  BLKSWAP 1, 1  |
			REVERSE, //  REVERSE 2, 0  |  REVERSE 3, 0
			XCHG,    //  XCHG S0 S1    |  XCHG S0 S2,

			TUCK,
			PUXC,
		};
		explicit Stack(Opcode opcode, int i = -1, int j = -1, int k = -1);
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const& _node) const override;
		Opcode opcode() const { return m_opcode; }
		int i() const { return m_i; }
		int j() const { return m_j; }
		int k() const { return m_k; }
	private:
		Opcode m_opcode{};
		int m_i{-1};
		int m_j{-1};
		int m_k{-1};
	};

	// abstract
	class Gen : public Inst {
	public:
		explicit Gen(bool _isPure) : m_isPure{_isPure} {}
		virtual int take() const = 0;
		virtual int ret() const = 0;
		bool isPure() const { return m_isPure; }
	private:
		bool m_isPure{}; // it doesn't throw exception, has no side effects (doesn't change any GLOB vars)
	};

	class Glob : public Gen {
	public:
		enum class Opcode {
			GetOrGetVar,
			SetOrSetVar,

			PUSHROOT,
			POPROOT,

			PUSH_C3,
			POP_C3,

			PUSH_C7,
			POP_C7,
		};
		explicit Glob(Opcode opcode, int index = -1);
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		Opcode opcode() const { return m_opcode; }
		int index() const { return m_index; }
		int take() const override;
		int ret() const override;
	private:
		Opcode m_opcode{};
		int m_index{-1};
	};

	class CodeBlock;

	class DeclRetFlag : public Inst {
	public:
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
	};

	class Opaque : public Gen {
	public:
		explicit Opaque(Pointer<CodeBlock> _block, int take, int ret, bool isPure) :
			Gen{isPure}, m_block(std::move(_block)), m_take{take}, m_ret{ret} {}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		Pointer<CodeBlock> const& block() const { return m_block; }
		int take() const override { return m_take; }
		int ret() const override { return m_ret; }
	private:
		Pointer<CodeBlock> m_block;
		int m_take{};
		int m_ret{};
	};

	class AsymGen : public Inst {
	public:
		explicit AsymGen(std::string opcode, int take, int retMin, int retMax) :
				m_opcode(std::move(opcode)), m_take{take}, m_retMin{retMin}, m_retMax{retMax} {}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		std::string const& opcode() const { return m_opcode; }
		int take() const { return m_take; }
		int retMin() const { return m_retMin; }
		int retMax() const { return m_retMax; }
	private:
		std::string m_opcode;
		int m_take{};
		int m_retMin{};
		int m_retMax{};
	};

	class HardCode : public Gen {
	public:
		explicit HardCode(std::vector<std::string> code, int take, int ret, bool _isPure) :
			Gen{_isPure}, m_code(std::move(code)), m_take{take}, m_ret{ret} {}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		std::vector<std::string> const& code() const { return m_code; }
		int take() const override { return m_take; }
		int ret() const override { return m_ret; }
	private:
		std::vector<std::string> m_code;
		int m_take{};
		int m_ret{};
	};

	class GenOpcode : public Gen {
	public:
		explicit GenOpcode(const std::string& opcode, int take, int ret, bool _isPure = false);
		void accept(TvmAstVisitor& _visitor) override;
		std::string fullOpcode() const;
		std::string const &opcode() const { return m_opcode; }
		std::string const &arg() const { return m_arg; }
		std::string const &comment() const { return m_comment; }
		int take() const override { return m_take; }
		int ret() const override { return m_ret; }
		bool operator==(TvmAstNode const& _node) const override;
	private:
		std::string m_opcode;
		std::string m_arg;
		std::string m_comment;
		int m_take;
		int m_ret;
	};

	class TvmReturn : public Inst {
	public:
		TvmReturn(bool _withIf, bool _withNot, bool _withAlt);
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		bool withIf() const { return m_withIf; }
		bool withNot() const { return m_withNot; }
		bool withAlt() const { return m_withAlt; }
	private:
		bool m_withIf{};
		bool m_withNot{};
		bool m_withAlt{};
	};

	class ReturnOrBreakOrCont : public Inst {
	public:
		explicit ReturnOrBreakOrCont(int _take, Pointer<CodeBlock> const &body) :
			m_take{_take},
			m_body{body}
		{
		}
		Pointer<CodeBlock> const &body() const { return m_body; }
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		int take() const { return m_take; }
	private:
		int m_take{};
		Pointer<CodeBlock> m_body;
	};

	class TvmException : public Inst {
	public:
		explicit TvmException(bool _arg, bool _any, bool _if, bool _not, const std::string& _param) :
			m_arg{_arg},
			m_any{_any},
			m_if{_if},
			m_not{_not},
			m_param{_param}
		{
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		std::string opcode() const;
		std::string const& arg() const { return m_param; }
		int take() const;
		bool withArg() const { return m_arg; }
		bool withAny() const { return m_any; }
		bool withIf() const { return m_if; }
		bool withNot() const { return m_not; }
	private:
		bool m_arg{};
		bool m_any{};
		bool m_if{};
		bool m_not{};
		std::string m_param;
	};

	class PushCellOrSlice : public Gen {
	public:
		enum class Type {
			PUSHREF_COMPUTE,
			PUSHREFSLICE_COMPUTE,
			PUSHREF,
			PUSHREFSLICE,
			CELL,
			PUSHSLICE
		};
		PushCellOrSlice(Type type, std::string blob, Pointer<PushCellOrSlice> child) :
			Gen{true}, // we don't execute data
			m_type{type},
			m_blob{std::move(blob)},
			m_child{std::move(child)}
		{
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		int take() const override { return 0; }
		int ret() const override { return 1; }
		Type type() const  { return m_type; }
		std::string const &blob() const { return m_blob; }
		Pointer<PushCellOrSlice> child() const { return m_child; };
		bool equal(PushCellOrSlice const& another) const;
	private:
		Type m_type;
		std::string m_blob;
		Pointer<PushCellOrSlice> m_child;
	};

	class CodeBlock : public Inst {
	public:
		enum class Type {
			None,
			PUSHCONT,
			PUSHREFCONT,
		};
		static std::string toString(Type t);
		CodeBlock(Type type, std::vector<Pointer<TvmAstNode>> instructions) :
			m_type{type}, m_instructions(std::move(instructions)) {
			for (const Pointer<TvmAstNode>& i : m_instructions) {
				solAssert(i != nullptr, "");
			}
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		Type type() const { return m_type; }
		std::vector<Pointer<TvmAstNode>> const& instructions() const { return m_instructions; }
		void upd(std::vector<Pointer<TvmAstNode>> instructions) { m_instructions = std::move(instructions); }
	private:
		Type m_type;
		std::vector<Pointer<TvmAstNode>> m_instructions;
	};

	class SubProgram : public Gen {
	public:
		SubProgram(int take, int ret, bool _isJmp, Pointer<CodeBlock> const &_block) :
			Gen{false},
			m_take{take},
			m_ret{ret},
			m_isJmp{_isJmp},
			m_block{_block}
		{
		}
		void accept(TvmAstVisitor &_visitor) override;
		bool operator==(TvmAstNode const&) const override;
		int take() const override { return  m_take; }
		int ret() const override { return m_ret; }
		Pointer<CodeBlock> const &block() const { return m_block; }
		bool isJmp() const { return m_isJmp; }
	private:
		int m_take{};
		int m_ret{};
		bool m_isJmp{};
		Pointer<CodeBlock> m_block;
	};

	// e.g.: b || f ? a + b : c / d;
	class TvmCondition : public Inst {
	public:
		TvmCondition(Pointer<CodeBlock> const &trueBody, Pointer<CodeBlock> const &falseBody, int ret) :
			mTrueBody{trueBody},
			mFalseBody{falseBody},
			m_ret{ret}
		{
		}
		int ret() const { return m_ret; }
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		Pointer<CodeBlock> const &trueBody() const { return mTrueBody; }
		Pointer<CodeBlock> const &falseBody() const { return mFalseBody; }
	private:
		Pointer<CodeBlock> mTrueBody;
		Pointer<CodeBlock> mFalseBody;
		int m_ret{};
	};

	// Take one value from stack and return one
	class LogCircuit : public TvmAstNode {
	public:
		enum class Type {
			AND,
			OR
		};
		LogCircuit(Type type, Pointer<CodeBlock> const &body) :
			m_type{type},
			m_body{body}
		{
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override;
		Type type() const { return m_type; }
		Pointer<CodeBlock> const &body() const { return m_body; }
	private:
		Type m_type{};
		Pointer<CodeBlock> m_body;
	};

	class TvmIfElse : public TvmAstNode {
	public:
		TvmIfElse(bool _withNot, bool _withJmp,
				  Pointer<CodeBlock> const &trueBody,
				  Pointer<CodeBlock> const &falseBody = nullptr);
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override { return false; } // TODO
		bool withNot() const { return m_withNot; }
		bool withJmp() const { return m_withJmp; }
		Pointer<CodeBlock> const& trueBody() const { return m_trueBody; }
		Pointer<CodeBlock> const& falseBody() const { return m_falseBody; }
	private:
		bool m_withNot{};
		bool m_withJmp{};
		Pointer<CodeBlock> m_trueBody;
		Pointer<CodeBlock> m_falseBody; // nullptr for if-statement
	};

	class TvmRepeat : public TvmAstNode {
	public:
		explicit TvmRepeat(bool _withBreakOrReturn, Pointer<CodeBlock> const &body) :
			m_withBreakOrReturn{_withBreakOrReturn},
			m_body(body)
		{
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override { return false; } // TODO
		bool withBreakOrReturn() const { return m_withBreakOrReturn; }
		Pointer<CodeBlock> const& body() const { return m_body; }
	private:
		bool m_withBreakOrReturn{};
		Pointer<CodeBlock> m_body;
	};

	class TvmUntil : public TvmAstNode {
	public:
		explicit TvmUntil(bool _withBreakOrReturn, Pointer<CodeBlock> const &body) :
			m_withBreakOrReturn{_withBreakOrReturn},
			m_body(body) { }
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override { return false; } // TODO
		bool withBreakOrReturn() const { return m_withBreakOrReturn; }
		Pointer<CodeBlock> const& body() const { return m_body; }
	private:
		bool m_withBreakOrReturn{};
		Pointer<CodeBlock> m_body;
	};

	class While : public TvmAstNode {
	public:
		While(bool _infinite, bool _withBreakOrReturn, Pointer<CodeBlock> const &condition,
			  Pointer<CodeBlock> const &body
	  	) :
			m_infinite{_infinite},
			m_withBreakOrReturn{_withBreakOrReturn},
			m_condition{condition},
			m_body(body) { }
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override { return false; } // TODO
		Pointer<CodeBlock> const& condition() const { return m_condition; }
		Pointer<CodeBlock> const& body() const { return m_body; }
		bool isInfinite() const { return m_infinite; }
		bool withBreakOrReturn() const { return m_withBreakOrReturn; }
	private:
		bool m_infinite{};
		bool m_withBreakOrReturn{};
		Pointer<CodeBlock> m_condition;
		Pointer<CodeBlock> m_body;
	};

	class Function : public TvmAstNode {
	public:
		enum class FunctionType {
			PrivateFunction,
			Macro,
			MacroGetter,
			MainInternal,
			MainExternal,
			OnCodeUpgrade,
			OnTickTock
		};
		Function(int take, int ret, std::string name, FunctionType type, Pointer<CodeBlock> block) :
			m_take{take},
			m_ret{ret},
			m_name(std::move(name)),
			m_type(type),
			m_block(std::move(block))
		{
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override { return false; } // TODO
		int take() const { return m_take; }
		int ret() const { return m_ret; }
		std::string const& name() const { return m_name; }
		FunctionType type() const { return m_type; }
		Pointer<CodeBlock> const& block() const { return m_block; }
	private:
		int m_take;
		int m_ret;
		std::string m_name;
		FunctionType m_type;
		Pointer<CodeBlock> m_block;
	};

	class Contract : public TvmAstNode {
	public:
		explicit Contract(
			std::vector<std::string> pragmas,
			std::vector<Pointer<Function>> functions
		) :
			m_pragmas{std::move(pragmas)},
			m_functions{std::move(functions)}
		{
		}
		void accept(TvmAstVisitor& _visitor) override;
		bool operator==(TvmAstNode const&) const override { return false; } // TODO
		std::vector<std::string> const &pragmas() const { return m_pragmas; }
		std::vector<Pointer<Function>>& functions();
	private:
		std::vector<std::string> m_pragmas;
		std::vector<Pointer<Function>> m_functions;
	};

	Pointer<GenOpcode> gen(const std::string& cmd);
	Pointer<PushCellOrSlice> genPushSlice(const std::string& data);
	Pointer<Stack> makeDROP(int cnt = 1);
	Pointer<Stack> makePOP(int i);
	Pointer<Stack> makeBLKPUSH(int qty, int index);
	Pointer<Stack> makePUSH(int i);
	Pointer<Stack> makePUSH2(int i, int j);
	Pointer<Stack> makePUSH3(int i, int j, int k);
	Pointer<TvmReturn> makeRET();
	Pointer<TvmReturn> makeRETALT();
	Pointer<TvmReturn> makeIFRETALT();
	Pointer<TvmReturn> makeIFRET();
	Pointer<TvmReturn> makeIFNOTRET();
	Pointer<TvmException> makeTHROW(const std::string& cmd);
	Pointer<Stack> makeXCH_S(int i);
	Pointer<Stack> makeXCH_S_S(int i, int j);
	Pointer<Glob> makeGetGlob(int i);
	Pointer<Glob> makeSetGlob(int i);
	Pointer<Stack> makeBLKDROP2(int droppedCount, int leftCount);
	Pointer<PushCellOrSlice> makePUSHREF(std::string data = "");
	Pointer<Stack> makeREVERSE(int i, int j);
	Pointer<Stack> makeROT();
	Pointer<Stack> makeROTREV();
	Pointer<Stack> makeBLKSWAP(int down, int top);
	Pointer<Stack> makeTUCK();
	Pointer<Stack> makePUXC(int i, int j);
	Pointer<TvmIfElse> makeRevert(TvmIfElse const& node);
	Pointer<TvmCondition> makeRevertCond(TvmCondition const& node);

	bool isPureGen01OrGetGlob(TvmAstNode const& node);
	bool isSWAP(Pointer<TvmAstNode> const& node);
	std::optional<std::pair<int, int>> isBLKSWAP(Pointer<TvmAstNode> const& node);
	std::optional<int> isDrop(Pointer<TvmAstNode> const& node);
	std::optional<int> isPOP(Pointer<TvmAstNode> const& node);
	bool isXCHG(Pointer<TvmAstNode> const& node, int i, int j);
	std::optional<int> isXCHG_S0(Pointer<TvmAstNode> const& node);
	std::optional<std::pair<int, int>> isREVERSE(Pointer<TvmAstNode> const& node);
	Pointer<PushCellOrSlice> isPlainPushSlice(Pointer<TvmAstNode> const& node);

}	// end solidity::frontend

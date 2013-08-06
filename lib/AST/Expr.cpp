//===--- Expr.cpp - Fortran Expressions -----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "flang/AST/Expr.h"
#include "flang/AST/ASTContext.h"
#include "flang/AST/Decl.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/StringRef.h"

namespace flang {

void APNumericStorage::setIntValue(ASTContext &C, const APInt &Val) {
  if (hasAllocation())
    C.Deallocate(pVal);

  BitWidth = Val.getBitWidth();
  unsigned NumWords = Val.getNumWords();
  const uint64_t* Words = Val.getRawData();
  if (NumWords > 1) {
    pVal = new (C) uint64_t[NumWords];
    std::copy(Words, Words + NumWords, pVal);
  } else if (NumWords == 1)
    VAL = Words[0];
  else
    VAL = 0;
}

SourceLocation ConstantExpr::getLocEnd() const {
  return MaxLoc;
}

IntegerConstantExpr::IntegerConstantExpr(ASTContext &C, SourceLocation Loc,
                                         SourceLocation MaxLoc, llvm::StringRef Data)
  : ConstantExpr(IntegerConstantExprClass, C.IntegerTy,  Loc, MaxLoc) {
  llvm::APInt Val(64,Data,10);
  Num.setValue(C, Val);
}

IntegerConstantExpr *IntegerConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                                 SourceLocation MaxLoc, llvm::StringRef Data) {
  return new (C) IntegerConstantExpr(C, Loc, MaxLoc, Data);
}

RealConstantExpr::RealConstantExpr(ASTContext &C, SourceLocation Loc,
                                   SourceLocation MaxLoc, llvm::StringRef Data,
                                   QualType Type)
  : ConstantExpr(RealConstantExprClass, Type, Loc, MaxLoc) {
  APFloat Val(C.getFPTypeSemantics(Type), Data);
  Num.setValue(C, Val);
}

RealConstantExpr *RealConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                           SourceLocation MaxLoc, llvm::StringRef Data,
                                           QualType Type) {
  return new (C) RealConstantExpr(C, Loc, MaxLoc, Data, Type);
}

ComplexConstantExpr::ComplexConstantExpr(ASTContext &C, SourceLocation Loc, SourceLocation MaxLoc,
                                         const APFloat &Re, const APFloat &Im, QualType Type)
  : ConstantExpr(ComplexConstantExprClass, Type, Loc, MaxLoc) {
  this->Re.setValue(C, Re);
  this->Im.setValue(C, Im);
}

ComplexConstantExpr *ComplexConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                                 SourceLocation MaxLoc,
                                                 const APFloat &Re, const APFloat &Im,
                                                 QualType Type) {
  return new (C) ComplexConstantExpr(C, Loc, MaxLoc, Re, Im, Type);
}

CharacterConstantExpr::CharacterConstantExpr(ASTContext &C, SourceLocation Loc,
                                             SourceLocation MaxLoc, llvm::StringRef data)
  : ConstantExpr(CharacterConstantExprClass, C.CharacterTy, Loc, MaxLoc) {
  // TODO: A 'kind' on a character literal constant.
  Data = new (C) char[data.size() + 1];
  std::strncpy(Data, data.data(), data.size());
  Data[data.size()] = '\0';
}

CharacterConstantExpr *CharacterConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                                     SourceLocation MaxLoc,
                                                     llvm::StringRef Data) {
  return new (C) CharacterConstantExpr(C, Loc, MaxLoc, Data);
}

BOZConstantExpr::BOZConstantExpr(ASTContext &C, SourceLocation Loc,
                                 SourceLocation MaxLoc, llvm::StringRef Data)
  : ConstantExpr(BOZConstantExprClass, C.IntegerTy, Loc, MaxLoc) {
  unsigned Radix = 0;
  switch (Data[0]) {
  case 'B':
    Kind = BinaryExprClass;
    Radix = 2;
    break;
  case 'O':
    Kind = Octal;
    Radix = 8;
    break;
  case 'Z': case 'X':
    Kind = Hexadecimal;
    Radix = 16;
    break;
  }

  size_t LastQuote = Data.rfind(Data[1]);
  assert(LastQuote == llvm::StringRef::npos && "Invalid BOZ constant!");
  llvm::StringRef NumStr = Data.slice(2, LastQuote);
  APInt Val;
  NumStr.getAsInteger(Radix, Val);
  Num.setValue(C, Val);
}

BOZConstantExpr *BOZConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                         SourceLocation MaxLoc, llvm::StringRef Data) {
  return new (C) BOZConstantExpr(C, Loc, MaxLoc, Data);
}

LogicalConstantExpr::LogicalConstantExpr(ASTContext &C, SourceLocation Loc,
                                         SourceLocation MaxLoc, llvm::StringRef Data)
  : ConstantExpr(LogicalConstantExprClass, C.LogicalTy, Loc, MaxLoc) {
  Val = (Data.compare_lower(".TRUE.") == 0);
}

LogicalConstantExpr *LogicalConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                                 SourceLocation MaxLoc, llvm::StringRef Data) {
  return new (C) LogicalConstantExpr(C, Loc, MaxLoc, Data);
}

RepeatedConstantExpr::RepeatedConstantExpr(SourceLocation Loc,
                                           IntegerConstantExpr *Repeat,
                                           Expr *Expression)
  : Expr(RepeatedConstantExprClass, Expression->getType(), Loc),
    RepeatCount(Repeat), E(Expression) {
}

RepeatedConstantExpr *RepeatedConstantExpr::Create(ASTContext &C, SourceLocation Loc,
                                                   IntegerConstantExpr *RepeatCount,
                                                   Expr* Expression) {
  return new (C) RepeatedConstantExpr(Loc, RepeatCount, Expression);
}

SourceLocation RepeatedConstantExpr::getLocStart() const {
  return RepeatCount->getLocStart();
}
SourceLocation RepeatedConstantExpr::getLocEnd() const {
  return E->getLocEnd();
}

MultiArgumentExpr::MultiArgumentExpr(ASTContext &C, ArrayRef<Expr*> Args) {
  NumArguments = Args.size();
  if(NumArguments == 0)
    Arguments = nullptr;
  else if(NumArguments == 1)
    Argument = Args[0];
  else {
    Arguments = new (C) Expr *[NumArguments];
    for (unsigned I = 0; I != NumArguments; ++I)
      Arguments[I] = Args[I];
  }
}

SourceLocation DesignatorExpr::getLocStart() const {
  return Target->getLocStart();
}

SubstringExpr::SubstringExpr(ASTContext &C, SourceLocation Loc, Expr *E,
                             Expr *Start, Expr *End)
  : DesignatorExpr(SubstringExprClass, C.CharacterTy, Loc, E),
    StartingPoint(Start), EndPoint(End) {
}

SubstringExpr *SubstringExpr::Create(ASTContext &C, SourceLocation Loc,
                                     Expr *Target, Expr *StartingPoint,
                                     Expr *EndPoint) {
  return new(C) SubstringExpr(C, Loc, Target, StartingPoint, EndPoint);
}


SourceLocation SubstringExpr::getLocEnd() const {
  if(EndPoint) return EndPoint->getLocEnd();
  else if(StartingPoint) return StartingPoint->getLocEnd();
  else return getLocation();
}

ArrayElementExpr::ArrayElementExpr(ASTContext &C, SourceLocation Loc, Expr *E,
                                   llvm::ArrayRef<Expr *> Subs)
  : DesignatorExpr(ArrayElementExprClass,
                   E->getType()->asArrayType()->getElementType(),
                   Loc, E),
    MultiArgumentExpr(C, Subs) {
}

ArrayElementExpr *ArrayElementExpr::Create(ASTContext &C, SourceLocation Loc,
                                           Expr *Target,
                                           llvm::ArrayRef<Expr *> Subscripts) {
  return new(C) ArrayElementExpr(C, Loc, Target, Subscripts);
}

SourceLocation ArrayElementExpr::getLocEnd() const {
  return getArguments().back()->getLocEnd();
}

VarExpr::VarExpr(SourceLocation Loc, const VarDecl *Var)
  : Expr(VarExprClass, Var->getType(), Loc),
    Variable(Var) {}

VarExpr *VarExpr::Create(ASTContext &C, SourceLocation Loc, VarDecl *VD) {
  VD->MarkUsedAsVariable(Loc);
  return new (C) VarExpr(Loc, VD);
}

SourceLocation VarExpr::getLocEnd() const {
  return SourceLocation::getFromPointer(getLocation().getPointer() +
                               Variable->getIdentifier()->getLength());
}

ReturnedValueExpr::ReturnedValueExpr(SourceLocation Loc, FunctionDecl *F)
  : Expr(ReturnedValueExprClass, F->getType(), Loc),Func(F) {
}

ReturnedValueExpr *ReturnedValueExpr::Create(ASTContext &C, SourceLocation Loc,
                                             FunctionDecl *Func) {
  return new(C) ReturnedValueExpr(Loc, Func);
}

SourceLocation ReturnedValueExpr::getLocEnd() const {
  return SourceLocation::getFromPointer(getLocation().getPointer() +
                               Func->getIdentifier()->getLength());
}

UnresolvedIdentifierExpr::UnresolvedIdentifierExpr(ASTContext &C,
                                                   SourceLocation Loc,
                                                   const IdentifierInfo *ID)
  : Expr(UnresolvedIdentifierExprClass, C.IntegerTy, Loc), IDInfo(ID) {
}

UnresolvedIdentifierExpr *UnresolvedIdentifierExpr::Create(ASTContext &C,
                                                           SourceLocation Loc,
                                                           const IdentifierInfo *IDInfo) {
  return new(C) UnresolvedIdentifierExpr(C, Loc, IDInfo);
}

SourceLocation UnresolvedIdentifierExpr::getLocEnd() const {
  return SourceLocation::getFromPointer(getLocation().getPointer() +
                                        IDInfo->getLength());
}


UnaryExpr *UnaryExpr::Create(ASTContext &C, SourceLocation Loc, Operator Op,
                             Expr *E) {
  return new (C) UnaryExpr(Expr::UnaryExprClass,
                           (Op != Not) ? E->getType() : C.LogicalTy,
                           Loc, Op, E);
}

SourceLocation UnaryExpr::getLocEnd() const {
  return E->getLocEnd();
}

DefinedUnaryOperatorExpr::DefinedUnaryOperatorExpr(SourceLocation Loc, Expr *E,
                                                   IdentifierInfo *IDInfo)
  : UnaryExpr(Expr::DefinedUnaryOperatorExprClass, E->getType(), Loc, Defined, E),
    II(IDInfo) {}

DefinedUnaryOperatorExpr *DefinedUnaryOperatorExpr::Create(ASTContext &C,
                                                           SourceLocation Loc,
                                                           Expr *E,
                                                           IdentifierInfo *IDInfo) {
  return new (C) DefinedUnaryOperatorExpr(Loc, E, IDInfo);
}

BinaryExpr *BinaryExpr::Create(ASTContext &C, SourceLocation Loc, Operator Op,
                               QualType Type, Expr *LHS, Expr *RHS) {
  return new (C) BinaryExpr(Expr::BinaryExprClass, Type, Loc, Op, LHS, RHS);
}

SourceLocation BinaryExpr::getLocStart() const {
  return LHS->getLocStart();
}

SourceLocation BinaryExpr::getLocEnd() const {
  return RHS->getLocEnd();
}

DefinedBinaryOperatorExpr *
DefinedBinaryOperatorExpr::Create(ASTContext &C, SourceLocation Loc, Expr *LHS,
                                  Expr *RHS, IdentifierInfo *IDInfo) {
  return new (C) DefinedBinaryOperatorExpr(Loc, LHS, RHS, IDInfo);
}

ImplicitCastExpr::ImplicitCastExpr(SourceLocation Loc, QualType Dest, Expr *e)
  : Expr(ImplicitCastExprClass,Dest,Loc),E(e) {
}

ImplicitCastExpr *ImplicitCastExpr::Create(ASTContext &C, SourceLocation Loc,
                                           QualType Dest, Expr *E) {
  return new(C) ImplicitCastExpr(Loc, Dest, E);
}

SourceLocation ImplicitCastExpr::getLocStart() const {
  return E->getLocStart();
}

SourceLocation ImplicitCastExpr::getLocEnd() const {
  return E->getLocEnd();
}

CallExpr::CallExpr(ASTContext &C, SourceLocation Loc,
                   FunctionDecl *Func, ArrayRef<Expr*> Args)
  : Expr(CallExprClass, Func->getType(), Loc), MultiArgumentExpr(C, Args),
    Function(Func) {
}

CallExpr *CallExpr::Create(ASTContext &C, SourceLocation Loc,
                           FunctionDecl *Func, ArrayRef<Expr*> Args) {
  return new(C) CallExpr(C, Loc, Func, Args);
}

SourceLocation CallExpr::getLocEnd() const {
  return getArguments().back()->getLocEnd();
}

IntrinsicCallExpr::
IntrinsicCallExpr(ASTContext &C, SourceLocation Loc,
                          intrinsic::FunctionKind Func,
                          ArrayRef<Expr*> Args,
                          QualType ReturnType)
  : Expr(IntrinsicCallExprClass, ReturnType, Loc),
    MultiArgumentExpr(C, Args), Function(Func) {
}

IntrinsicCallExpr *IntrinsicCallExpr::
Create(ASTContext &C, SourceLocation Loc,
       intrinsic::FunctionKind Func,
       ArrayRef<Expr*> Arguments,
       QualType ReturnType) {
  return new(C) IntrinsicCallExpr(C, Loc, Func, Arguments,
                                          ReturnType);
}

SourceLocation IntrinsicCallExpr::getLocEnd() const {
  return getArguments().back()->getLocEnd();
}

ImpliedDoExpr::ImpliedDoExpr(ASTContext &C, SourceLocation Loc,
                             VarDecl *Var, ArrayRef<Expr*> Body,
                             Expr *InitialParam, Expr *TerminalParam,
                             Expr *IncrementationParam)
  : Expr(ImpliedDoExprClass, QualType(), Loc), DoVar(Var),
    DoList(C, Body), Init(InitialParam), Terminate(TerminalParam),
    Increment(IncrementationParam) {
}

ImpliedDoExpr *ImpliedDoExpr::Create(ASTContext &C, SourceLocation Loc,
                                     VarDecl *DoVar, ArrayRef<Expr*> Body,
                                     Expr *InitialParam, Expr *TerminalParam,
                                     Expr *IncrementationParam) {
  return new(C) ImpliedDoExpr(C, Loc, DoVar, Body, InitialParam,
                              TerminalParam, IncrementationParam);
}

SourceLocation ImpliedDoExpr::getLocEnd() const {
  return Terminate->getLocEnd();
}

ArrayConstructorExpr::ArrayConstructorExpr(ASTContext &C, SourceLocation Loc,
                                           ArrayRef<Expr*> Items, QualType Ty)
  : Expr(ArrayConstructorExprClass, Ty, Loc),
    MultiArgumentExpr(C, Items) {
}

ArrayConstructorExpr *ArrayConstructorExpr::Create(ASTContext &C, SourceLocation Loc,
                                                   ArrayRef<Expr*> Items, QualType Ty) {
  return new(C) ArrayConstructorExpr(C, Loc, Items, Ty);
}

SourceLocation ArrayConstructorExpr::getLocEnd() const {
  if(getItems().empty())
    return getLocation();
  return getItems().back()->getLocEnd();
}

RangeExpr::RangeExpr(SourceLocation Loc, Expr *First, Expr *Second)
  : Expr(RangeExprClass, QualType(), Loc), E1(First), E2(Second) {
}

RangeExpr *RangeExpr::Create(ASTContext &C, SourceLocation Loc,
                             Expr *First, Expr *Second) {
  return new(C) RangeExpr(Loc, First, Second);
}

void RangeExpr::setFirstExpr(Expr *E) {
  E1 = E;
}

void RangeExpr::setSecondExpr(Expr *E) {
  E2 = E;
}

SourceLocation RangeExpr::getLocStart() const {
  if(hasFirstExpr()) return E1->getLocStart();
  return getLocation();
}

SourceLocation RangeExpr::getLocEnd() const {
  if(hasSecondExpr()) return E2->getLocEnd();
  return getLocation();
}

//===----------------------------------------------------------------------===//
// Array Specification
//===----------------------------------------------------------------------===//

ArraySpec::ArraySpec(ArraySpecKind K)
  : Kind(K) {}

ExplicitShapeSpec::ExplicitShapeSpec(Expr *UB)
  : ArraySpec(k_ExplicitShape), LowerBound(nullptr), UpperBound(UB) {}
ExplicitShapeSpec::ExplicitShapeSpec(Expr *LB, Expr *UB)
  : ArraySpec(k_ExplicitShape), LowerBound(LB), UpperBound(UB) {}

ExplicitShapeSpec *ExplicitShapeSpec::Create(ASTContext &C, Expr *UB) {
  return new (C) ExplicitShapeSpec(UB);
}

ExplicitShapeSpec *ExplicitShapeSpec::Create(ASTContext &C,
                                             Expr *LB, Expr *UB) {
  return new (C) ExplicitShapeSpec(LB, UB);
}

AssumedShapeSpec::AssumedShapeSpec()
  : ArraySpec(k_AssumedShape), LowerBound() {}
AssumedShapeSpec::AssumedShapeSpec(Expr *LB)
  : ArraySpec(k_AssumedShape), LowerBound(LB) {}

AssumedShapeSpec *AssumedShapeSpec::Create(ASTContext &C) {
  return new (C) AssumedShapeSpec();
}

AssumedShapeSpec *AssumedShapeSpec::Create(ASTContext &C, Expr *LB) {
  return new (C) AssumedShapeSpec(LB);
}

DeferredShapeSpec::DeferredShapeSpec()
  : ArraySpec(k_DeferredShape) {}

DeferredShapeSpec *DeferredShapeSpec::Create(ASTContext &C) {
  return new (C) DeferredShapeSpec();
}

ImpliedShapeSpec::ImpliedShapeSpec(SourceLocation L)
  : ArraySpec(k_ImpliedShape), Loc(L), LowerBound() {}
ImpliedShapeSpec::ImpliedShapeSpec(SourceLocation L, Expr *LB)
  : ArraySpec(k_ImpliedShape), Loc(L), LowerBound(LB) {}

ImpliedShapeSpec *ImpliedShapeSpec::Create(ASTContext &C, SourceLocation Loc) {
  return new (C) ImpliedShapeSpec(Loc);
}

ImpliedShapeSpec *ImpliedShapeSpec::Create(ASTContext &C, SourceLocation Loc, Expr *LB) {
  return new (C) ImpliedShapeSpec(Loc, LB);
}

} //namespace flang

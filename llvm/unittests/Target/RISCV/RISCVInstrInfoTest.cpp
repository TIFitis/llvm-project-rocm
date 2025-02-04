//===- RISCVInstrInfoTest.cpp - RISCVInstrInfo unit tests -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "RISCVTargetMachine.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include "gtest/gtest.h"

#include <memory>

using namespace llvm;

namespace {

class RISCVInstrInfoTest : public testing::TestWithParam<const char *> {
protected:
  std::unique_ptr<RISCVTargetMachine> TM;
  std::unique_ptr<LLVMContext> Ctx;
  std::unique_ptr<RISCVSubtarget> ST;
  std::unique_ptr<MachineModuleInfo> MMI;
  std::unique_ptr<MachineFunction> MF;

  static void SetUpTestSuite() {
    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTarget();
    LLVMInitializeRISCVTargetMC();
  }

  RISCVInstrInfoTest() {
    std::string Error;
    auto TT(Triple::normalize(GetParam()));
    const Target *TheTarget = TargetRegistry::lookupTarget(TT, Error);
    TargetOptions Options;

    TM.reset(static_cast<RISCVTargetMachine *>(TheTarget->createTargetMachine(
        TT, "generic", "", Options, std::nullopt, std::nullopt,
        CodeGenOptLevel::Default)));

    Ctx = std::make_unique<LLVMContext>();
    Module M("Module", *Ctx);
    M.setDataLayout(TM->createDataLayout());
    auto *FType = FunctionType::get(Type::getVoidTy(*Ctx), false);
    auto *F = Function::Create(FType, GlobalValue::ExternalLinkage, "Test", &M);
    MMI = std::make_unique<MachineModuleInfo>(TM.get());

    ST = std::make_unique<RISCVSubtarget>(
        TM->getTargetTriple(), TM->getTargetCPU(), TM->getTargetCPU(),
        TM->getTargetFeatureString(),
        TM->getTargetTriple().isArch64Bit() ? "lp64" : "ilp32", 0, 0, *TM);

    MF = std::make_unique<MachineFunction>(*F, *TM, *ST, 42, *MMI);
  }
};

TEST_P(RISCVInstrInfoTest, IsAddImmediate) {
  const RISCVInstrInfo *TII = ST->getInstrInfo();
  DebugLoc DL;

  MachineInstr *MI1 = BuildMI(*MF, DL, TII->get(RISCV::ADDI), RISCV::X1)
                          .addReg(RISCV::X2)
                          .addImm(-128)
                          .getInstr();
  auto MI1Res = TII->isAddImmediate(*MI1, RISCV::X1);
  ASSERT_TRUE(MI1Res.has_value());
  EXPECT_EQ(MI1Res->Reg, RISCV::X2);
  EXPECT_EQ(MI1Res->Imm, -128);
  EXPECT_FALSE(TII->isAddImmediate(*MI1, RISCV::X2).has_value());

  MachineInstr *MI2 =
      BuildMI(*MF, DL, TII->get(RISCV::LUI), RISCV::X1).addImm(-128).getInstr();
  EXPECT_FALSE(TII->isAddImmediate(*MI2, RISCV::X1));

  // Check ADDIW isn't treated as isAddImmediate.
  if (ST->is64Bit()) {
    MachineInstr *MI3 = BuildMI(*MF, DL, TII->get(RISCV::ADDIW), RISCV::X1)
                            .addReg(RISCV::X2)
                            .addImm(-128)
                            .getInstr();
    EXPECT_FALSE(TII->isAddImmediate(*MI3, RISCV::X1));
  }
}

TEST_P(RISCVInstrInfoTest, GetMemOperandsWithOffsetWidth) {
  const RISCVInstrInfo *TII = ST->getInstrInfo();
  const TargetRegisterInfo *TRI = ST->getRegisterInfo();
  DebugLoc DL;

  SmallVector<const MachineOperand *> BaseOps;
  unsigned Width;
  int64_t Offset;
  bool OffsetIsScalable;

  auto MMO = MF->getMachineMemOperand(MachinePointerInfo(),
                                      MachineMemOperand::MOLoad, 1, Align(1));
  MachineInstr *MI = BuildMI(*MF, DL, TII->get(RISCV::LB), RISCV::X1)
                         .addReg(RISCV::X2)
                         .addImm(-128)
                         .addMemOperand(MMO)
                         .getInstr();
  bool Res = TII->getMemOperandsWithOffsetWidth(*MI, BaseOps, Offset,
                                                OffsetIsScalable, Width, TRI);
  ASSERT_TRUE(Res);
  ASSERT_EQ(BaseOps.size(), 1u);
  ASSERT_TRUE(BaseOps.front()->isReg());
  EXPECT_EQ(BaseOps.front()->getReg(), RISCV::X2);
  EXPECT_EQ(Offset, -128);
  EXPECT_FALSE(OffsetIsScalable);
  EXPECT_EQ(Width, 1u);

  BaseOps.clear();
  MMO = MF->getMachineMemOperand(MachinePointerInfo(),
                                 MachineMemOperand::MOStore, 4, Align(4));
  MI = BuildMI(*MF, DL, TII->get(RISCV::FSW))
           .addReg(RISCV::F3_F)
           .addReg(RISCV::X3)
           .addImm(36)
           .addMemOperand(MMO);
  Res = TII->getMemOperandsWithOffsetWidth(*MI, BaseOps, Offset,
                                           OffsetIsScalable, Width, TRI);
  ASSERT_TRUE(Res);
  ASSERT_EQ(BaseOps.size(), 1u);
  ASSERT_TRUE(BaseOps.front()->isReg());
  EXPECT_EQ(BaseOps.front()->getReg(), RISCV::X3);
  EXPECT_EQ(Offset, 36);
  EXPECT_FALSE(OffsetIsScalable);
  EXPECT_EQ(Width, 4u);

  BaseOps.clear();
  MMO = MF->getMachineMemOperand(MachinePointerInfo(),
                                 MachineMemOperand::MOStore, 16, Align(16));
  MI = BuildMI(*MF, DL, TII->get(RISCV::PseudoVLE32_V_M1), RISCV::V8)
           .addReg(RISCV::X3)
           .addMemOperand(MMO);
  Res = TII->getMemOperandsWithOffsetWidth(*MI, BaseOps, Offset,
                                           OffsetIsScalable, Width, TRI);
  ASSERT_FALSE(Res); // Vector loads/stored are not handled for now.

  BaseOps.clear();
  MI = BuildMI(*MF, DL, TII->get(RISCV::ADDI), RISCV::X4)
           .addReg(RISCV::X5)
           .addImm(16);
  Res = TII->getMemOperandsWithOffsetWidth(*MI, BaseOps, Offset,
                                           OffsetIsScalable, Width, TRI);

  // TODO: AArch64 can handle this case, and we probably should too.
  BaseOps.clear();
  MMO = MF->getMachineMemOperand(MachinePointerInfo(),
                                 MachineMemOperand::MOStore, 4, Align(4));
  MI = BuildMI(*MF, DL, TII->get(RISCV::SW))
           .addReg(RISCV::X3)
           .addFrameIndex(2)
           .addImm(4)
           .addMemOperand(MMO);
  Res = TII->getMemOperandsWithOffsetWidth(*MI, BaseOps, Offset,
                                           OffsetIsScalable, Width, TRI);
  EXPECT_FALSE(Res);
}

} // namespace

INSTANTIATE_TEST_SUITE_P(RV32And64, RISCVInstrInfoTest,
                         testing::Values("riscv32", "riscv64"));

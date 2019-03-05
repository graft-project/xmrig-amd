/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2019 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2018      Lee Clagett <https://github.com/vtnerd>
 * Copyright 2018-2019 SChernykh   <https://github.com/SChernykh>
 * Copyright 2016-2019 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <assert.h>
#include <sstream>


#include "common/cpu/Cpu.h"
#include "common/log/Log.h"
#include "common/net/Job.h"
#include "Mem.h"
#include "crypto/CryptoNight.h"
#include "crypto/CryptoNight_test.h"
#include "crypto/CryptoNight_x86.h"
#include "net/JobResult.h"


alignas(16) cryptonight_ctx *CryptoNight::m_ctx = nullptr;
xmrig::Algo CryptoNight::m_algorithm = xmrig::CRYPTONIGHT;
xmrig::AlgoVerify CryptoNight::m_av  = xmrig::VERIFY_HW_AES;


bool CryptoNight::hash(const xmrig::Job &job, xmrig::JobResult &result, cryptonight_ctx *ctx)
{
    fn(job.algorithm().variant())(job.blob(), job.size(), result.result, &ctx, job.height());

    return *reinterpret_cast<uint64_t*>(result.result + 24) < job.target();
}


#ifndef XMRIG_NO_ASM
xmrig::CpuThread::cn_mainloop_fun cn_half_mainloop_ivybridge_asm = nullptr;
xmrig::CpuThread::cn_mainloop_fun cn_half_mainloop_ryzen_asm = nullptr;
xmrig::CpuThread::cn_mainloop_fun cn_half_mainloop_bulldozer_asm = nullptr;
xmrig::CpuThread::cn_mainloop_double_fun cn_half_double_mainloop_sandybridge_asm = nullptr;

xmrig::CpuThread::cn_mainloop_fun        cn_trtl_mainloop_ivybridge_asm;
xmrig::CpuThread::cn_mainloop_fun        cn_trtl_mainloop_ryzen_asm;
xmrig::CpuThread::cn_mainloop_fun        cn_trtl_mainloop_bulldozer_asm;
xmrig::CpuThread::cn_mainloop_double_fun cn_trtl_double_mainloop_sandybridge_asm;

template<typename T, typename U>
static void patchCode(T dst, U src, const uint32_t iterations, const uint32_t mask)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(src);

    // Workaround for Visual Studio placing trampoline in debug builds.
#   if defined(_MSC_VER)
    if (p[0] == 0xE9) {
        p += *(int32_t*)(p + 1) + 5;
    }
#   endif

    size_t size = 0;
    while (*(uint32_t*)(p + size) != 0xDEADC0DE) {
        ++size;
    }
    size += sizeof(uint32_t);

    memcpy((void*)dst, (const void*)src, size);

    uint8_t* patched_data = reinterpret_cast<uint8_t*>(dst);
    for (size_t i = 0; i + sizeof(uint32_t) <= size; ++i) {
        switch (*(uint32_t*)(patched_data + i)) {
        case xmrig::CRYPTONIGHT_ITER:
            *(uint32_t*)(patched_data + i) = iterations;
            break;

        case xmrig::CRYPTONIGHT_MASK:
            *(uint32_t*)(patched_data + i) = mask;
            break;
        }
    }
}

static void patchAsmVariants()
{
    const int allocation_size = 65536;
    uint8_t *base = static_cast<uint8_t *>(Mem::allocateExecutableMemory(allocation_size));

    cn_half_mainloop_ivybridge_asm              = reinterpret_cast<xmrig::CpuThread::cn_mainloop_fun>         (base + 0x0000);
    cn_half_mainloop_ryzen_asm                  = reinterpret_cast<xmrig::CpuThread::cn_mainloop_fun>         (base + 0x1000);
    cn_half_mainloop_bulldozer_asm              = reinterpret_cast<xmrig::CpuThread::cn_mainloop_fun>         (base + 0x2000);
    cn_half_double_mainloop_sandybridge_asm     = reinterpret_cast<xmrig::CpuThread::cn_mainloop_double_fun>  (base + 0x3000);

    cn_trtl_mainloop_ivybridge_asm              = reinterpret_cast<xmrig::CpuThread::cn_mainloop_fun>         (base + 0x4000);
    cn_trtl_mainloop_ryzen_asm                  = reinterpret_cast<xmrig::CpuThread::cn_mainloop_fun>         (base + 0x5000);
    cn_trtl_mainloop_bulldozer_asm              = reinterpret_cast<xmrig::CpuThread::cn_mainloop_fun>         (base + 0x6000);
    cn_trtl_double_mainloop_sandybridge_asm     = reinterpret_cast<xmrig::CpuThread::cn_mainloop_double_fun>  (base + 0x7000);

    patchCode(cn_half_mainloop_ivybridge_asm,           cnv2_mainloop_ivybridge_asm,            xmrig::CRYPTONIGHT_HALF_ITER,   xmrig::CRYPTONIGHT_MASK);
    patchCode(cn_half_mainloop_ryzen_asm,               cnv2_mainloop_ryzen_asm,                xmrig::CRYPTONIGHT_HALF_ITER,   xmrig::CRYPTONIGHT_MASK);
    patchCode(cn_half_mainloop_bulldozer_asm,           cnv2_mainloop_bulldozer_asm,            xmrig::CRYPTONIGHT_HALF_ITER,   xmrig::CRYPTONIGHT_MASK);
    patchCode(cn_half_double_mainloop_sandybridge_asm,  cnv2_double_mainloop_sandybridge_asm,   xmrig::CRYPTONIGHT_HALF_ITER,   xmrig::CRYPTONIGHT_MASK);

    patchCode(cn_trtl_mainloop_ivybridge_asm,           cnv2_mainloop_ivybridge_asm,            xmrig::CRYPTONIGHT_TRTL_ITER,   xmrig::CRYPTONIGHT_PICO_MASK);
    patchCode(cn_trtl_mainloop_ryzen_asm,               cnv2_mainloop_ryzen_asm,                xmrig::CRYPTONIGHT_TRTL_ITER,   xmrig::CRYPTONIGHT_PICO_MASK);
    patchCode(cn_trtl_mainloop_bulldozer_asm,           cnv2_mainloop_bulldozer_asm,            xmrig::CRYPTONIGHT_TRTL_ITER,   xmrig::CRYPTONIGHT_PICO_MASK);
    patchCode(cn_trtl_double_mainloop_sandybridge_asm,  cnv2_double_mainloop_sandybridge_asm,   xmrig::CRYPTONIGHT_TRTL_ITER,   xmrig::CRYPTONIGHT_PICO_MASK);

    Mem::protectExecutableMemory(base, allocation_size);
    Mem::flushInstructionCache(base, allocation_size);
}
#endif

bool CryptoNight::init(xmrig::Algo algorithm)
{
#ifndef XMRIG_NO_ASM
    patchAsmVariants();
#endif

    m_algorithm = algorithm;
    m_av        = xmrig::Cpu::info()->hasAES() ? xmrig::VERIFY_HW_AES : xmrig::VERIFY_SOFT_AES;

    return selfTest();
}


CryptoNight::cn_hash_fun CryptoNight::fn(xmrig::Algo algorithm, xmrig::AlgoVerify av, xmrig::Variant variant)
{
    using namespace xmrig;

    assert(variant >= VARIANT_0 && variant < VARIANT_MAX);

    static const cn_hash_fun func_table[] = {
        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_0>,
        cryptonight_single_hash<CRYPTONIGHT, true,  VARIANT_0>,

        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_1>,
        cryptonight_single_hash<CRYPTONIGHT, true,  VARIANT_1>,

        nullptr, nullptr, // VARIANT_TUBE

        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_XTL>,
        cryptonight_single_hash<CRYPTONIGHT, true,  VARIANT_XTL>,

        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_MSR>,
        cryptonight_single_hash<CRYPTONIGHT, true,  VARIANT_MSR>,

        nullptr, nullptr, // VARIANT_XHV

        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_XAO>,
        cryptonight_single_hash<CRYPTONIGHT, true,  VARIANT_XAO>,

        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_RTO>,
        cryptonight_single_hash<CRYPTONIGHT, true,  VARIANT_RTO>,

#       ifdef XMRIG_NO_ASM
        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_2>,
#       else
        cryptonight_single_hash_asm<CRYPTONIGHT, VARIANT_2, ASM_AUTO>,
#       endif
        cryptonight_single_hash<CRYPTONIGHT, true, VARIANT_2>,

#       ifdef XMRIG_NO_ASM
        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_HALF>,
#       else
        cryptonight_single_hash_asm<CRYPTONIGHT, VARIANT_HALF, ASM_AUTO>,
#       endif
        cryptonight_single_hash<CRYPTONIGHT, true, VARIANT_HALF>,

        nullptr, nullptr, // VARIANT_TRTL

#       ifndef XMRIG_NO_CN_GPU
        cryptonight_single_hash_gpu<CRYPTONIGHT, false, VARIANT_GPU>,
        cryptonight_single_hash_gpu<CRYPTONIGHT, true,  VARIANT_GPU>,
#       else
        nullptr, nullptr, // VARIANT_GPU
#       endif

#       ifdef XMRIG_NO_ASM
        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_WOW>,
#       else
        cryptonight_single_hash_asm<CRYPTONIGHT, VARIANT_WOW, ASM_AUTO>,
#       endif
        cryptonight_single_hash<CRYPTONIGHT, true, VARIANT_WOW>,

#       ifdef XMRIG_NO_ASM
        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_4>,
#       else
        cryptonight_single_hash_asm<CRYPTONIGHT, VARIANT_4, ASM_AUTO>,
#       endif
        cryptonight_single_hash<CRYPTONIGHT, true, VARIANT_4>,

#       ifdef XMRIG_NO_ASM
        cryptonight_single_hash<CRYPTONIGHT, false, VARIANT_RWZ>,
#       else
        cryptonight_single_hash_asm<CRYPTONIGHT, VARIANT_RWZ, ASM_AUTO>,
#       endif
        cryptonight_single_hash<CRYPTONIGHT, true, VARIANT_RWZ>,

#       ifndef XMRIG_NO_AEON
        cryptonight_single_hash<CRYPTONIGHT_LITE, false, VARIANT_0>,
        cryptonight_single_hash<CRYPTONIGHT_LITE, true,  VARIANT_0>,

        cryptonight_single_hash<CRYPTONIGHT_LITE, false, VARIANT_1>,
        cryptonight_single_hash<CRYPTONIGHT_LITE, true,  VARIANT_1>,

        nullptr, nullptr, // VARIANT_TUBE
        nullptr, nullptr, // VARIANT_XTL
        nullptr, nullptr, // VARIANT_MSR
        nullptr, nullptr, // VARIANT_XHV
        nullptr, nullptr, // VARIANT_XAO
        nullptr, nullptr, // VARIANT_RTO
        nullptr, nullptr, // VARIANT_2
        nullptr, nullptr, // VARIANT_HALF
        nullptr, nullptr, // VARIANT_TRTL
        nullptr, nullptr, // VARIANT_GPU
        nullptr, nullptr, // VARIANT_WOW
        nullptr, nullptr, // VARIANT_4
        nullptr, nullptr, // VARIANT_RWZ
#       else
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr,
#       endif

#       ifndef XMRIG_NO_SUMO
        cryptonight_single_hash<CRYPTONIGHT_HEAVY, false, VARIANT_0>,
        cryptonight_single_hash<CRYPTONIGHT_HEAVY, true,  VARIANT_0>,

        nullptr, nullptr, // VARIANT_1

        cryptonight_single_hash<CRYPTONIGHT_HEAVY, false, VARIANT_TUBE>,
        cryptonight_single_hash<CRYPTONIGHT_HEAVY, true,  VARIANT_TUBE>,

        nullptr, nullptr, // VARIANT_XTL
        nullptr, nullptr, // VARIANT_MSR

        cryptonight_single_hash<CRYPTONIGHT_HEAVY, false, VARIANT_XHV>,
        cryptonight_single_hash<CRYPTONIGHT_HEAVY, true,  VARIANT_XHV>,

        nullptr, nullptr, // VARIANT_XAO
        nullptr, nullptr, // VARIANT_RTO
        nullptr, nullptr, // VARIANT_2
        nullptr, nullptr, // VARIANT_HALF
        nullptr, nullptr, // VARIANT_TRTL
        nullptr, nullptr, // VARIANT_GPU
        nullptr, nullptr, // VARIANT_WOW
        nullptr, nullptr, // VARIANT_4
        nullptr, nullptr, // VARIANT_RWZ
#       else
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr,
#       endif
#       ifndef XMRIG_NO_CN_PICO
        nullptr, nullptr, // VARIANT_0
        nullptr, nullptr, // VARIANT_1
        nullptr, nullptr, // VARIANT_TUBE
        nullptr, nullptr, // VARIANT_XTL
        nullptr, nullptr, // VARIANT_MSR
        nullptr, nullptr, // VARIANT_XHV
        nullptr, nullptr, // VARIANT_XAO
        nullptr, nullptr, // VARIANT_RTO
        nullptr, nullptr, // VARIANT_2
        nullptr, nullptr, // VARIANT_HALF

#       ifdef XMRIG_NO_ASM
        cryptonight_single_hash<CRYPTONIGHT_PICO, false, VARIANT_TRTL>,
#       else
        cryptonight_single_hash_asm<CRYPTONIGHT_PICO, VARIANT_TRTL, ASM_AUTO>,
#       endif
        cryptonight_single_hash<CRYPTONIGHT_PICO, true, VARIANT_TRTL>,

        nullptr, nullptr, // VARIANT_GPU
        nullptr, nullptr, // VARIANT_WOW
        nullptr, nullptr, // VARIANT_4
        nullptr, nullptr, // VARIANT_RWZ
    #else
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr,
#       endif
    };

    static_assert((VARIANT_MAX * 2 * ALGO_MAX) == sizeof(func_table) / sizeof(func_table[0]), "func_table size mismatch");

    const size_t index = VARIANT_MAX * 2 * algorithm + 2 * variant + av - 1;

#   ifndef NDEBUG
    cn_hash_fun func = func_table[index];

    assert(index < sizeof(func_table) / sizeof(func_table[0]));
    assert(func != nullptr);

    return func;
#   else
    return func_table[index];
#   endif
}


bool CryptoNight::selfTest() {
    using namespace xmrig;

    Mem::create(&m_ctx, m_algorithm, 1);

    if (m_algorithm == xmrig::CRYPTONIGHT) {
        if (!verify2(VARIANT_WOW, test_input_WOW)) {
            LOG_WARN("CryptonightR (Wownero) self-test failed");
            return false;
        }
        if (!verify2(VARIANT_4, test_input_R)) {
            LOG_WARN("CryptonightR self-test failed");
            return false;
        }

        return verify(VARIANT_0, test_output_v0)    &&
               verify(VARIANT_1,    test_output_v1)  &&
               verify(VARIANT_2,    test_output_v2)  &&
               verify(VARIANT_XTL,  test_output_xtl) &&
               verify(VARIANT_MSR,  test_output_msr) &&
               verify(VARIANT_XAO,  test_output_xao) &&
               verify(VARIANT_RTO,  test_output_rto) &&
#              ifndef XMRIG_NO_CN_GPU
               verify(VARIANT_GPU,  test_output_gpu) &&
#              endif
               verify(VARIANT_HALF, test_output_half) &&
               verify(VARIANT_RWZ, test_output_rwz);
    }

#   ifndef XMRIG_NO_AEON
    if (m_algorithm == xmrig::CRYPTONIGHT_LITE) {
        return verify(VARIANT_0, test_output_v0_lite) &&
               verify(VARIANT_1, test_output_v1_lite);
    }
#   endif

#   ifndef XMRIG_NO_SUMO
    if (m_algorithm == xmrig::CRYPTONIGHT_HEAVY) {
        return verify(VARIANT_0,    test_output_v0_heavy)  &&
               verify(VARIANT_XHV,  test_output_xhv_heavy) &&
               verify(VARIANT_TUBE, test_output_tube_heavy);
    }
#   endif

#   ifndef XMRIG_NO_CN_PICO
    if (m_algorithm == xmrig::CRYPTONIGHT_PICO) {
        return verify(VARIANT_TRTL, test_output_pico_trtl);
    }
#   endif

    return false;
}


bool CryptoNight::verify(xmrig::Variant variant, const uint8_t *referenceValue)
{
    if (!m_ctx) {
        return false;
    }

    uint8_t output[32];

    cn_hash_fun func = fn(variant);
    if (!func) {
        return false;
    }

    func(test_input, 76, output, &m_ctx, 0);

    return memcmp(output, referenceValue, 32) == 0;
}

bool CryptoNight::verify2(xmrig::Variant variant, const char *test_data)
{
    cn_hash_fun func = fn(variant);
    if (!func) {
        return false;
    }

    std::stringstream s(test_data);
    std::string expected_hex;
    std::string input_hex;
    uint64_t height;
    while (!s.eof())
    {
        uint8_t referenceValue[32];
        uint8_t input[256];

        s >> expected_hex;
        s >> input_hex;
        s >> height;

        if ((expected_hex.length() != 64) || (input_hex.length() > 512))
        {
            return false;
        }

        bool err = false;

        for (int i = 0; i < 32; ++i)
        {
            referenceValue[i] = (hf_hex2bin(expected_hex[i * 2], err) << 4) + hf_hex2bin(expected_hex[i * 2 + 1], err);
        }

        const size_t input_len = input_hex.length() / 2;
        for (size_t i = 0; i < input_len; ++i)
        {
            input[i] = (hf_hex2bin(input_hex[i * 2], err) << 4) + hf_hex2bin(input_hex[i * 2 + 1], err);
        }

        if (err)
        {
            return false;
        }

        uint8_t hash[32];
        func(input, input_len, hash, &m_ctx, height);
        if (memcmp(hash, referenceValue, sizeof(hash)) != 0)
        {
            return false;
        }
    }
    return true;
}

/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-dyna-processor
 * Created on: 3 авг. 2021 г.
 *
 * lsp-plugins-dyna-processor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-dyna-processor is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-dyna-processor. If not, see <https://www.gnu.org/licenses/>.
 */

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/dyna_processor.h>

#define LSP_PLUGINS_DYNA_PROCESSOR_VERSION_MAJOR       1
#define LSP_PLUGINS_DYNA_PROCESSOR_VERSION_MINOR       0
#define LSP_PLUGINS_DYNA_PROCESSOR_VERSION_MICRO       28

#define LSP_PLUGINS_DYNA_PROCESSOR_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_DYNA_PROCESSOR_VERSION_MAJOR, \
        LSP_PLUGINS_DYNA_PROCESSOR_VERSION_MINOR, \
        LSP_PLUGINS_DYNA_PROCESSOR_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Dynamics Processor
        static const int plugin_classes[] =
        {
            C_DYNAMICS, -1
        };

        static const int clap_features_mono[]       = { CF_AUDIO_EFFECT, CF_MONO, -1 };
        static const int clap_features_stereo[]     = { CF_AUDIO_EFFECT, CF_STEREO, -1 };

        static const port_item_t dyna_proc_sc_modes[] =
        {
            { "Peak",           "sidechain.peak"            },
            { "RMS",            "sidechain.rms"             },
            { "LPF",            "sidechain.lpf"             },
            { "SMA",            "sidechain.sma"             },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_sc_sources[] =
        {
            { "Middle",         "sidechain.middle"          },
            { "Side",           "sidechain.side"            },
            { "Left",           "sidechain.left"            },
            { "Right",          "sidechain.right"           },
            { "Min",            "sidechain.min"             },
            { "Max",            "sidechain.max"             },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_sc_split_sources[] =
        {
            { "Left/Right",     "sidechain.left_right"      },
            { "Right/Left",     "sidechain.right_left"      },
            { "Mid/Side",       "sidechain.mid_side"        },
            { "Side/Mid",       "sidechain.side_mid"        },
            { "Min",            "sidechain.min"             },
            { "Max",            "sidechain.max"             },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_sc_type[] =
        {
            { "Feed-forward",   "sidechain.feed_forward"    },
            { "Feed-back",      "sidechain.feed_back"       },
            { "Link",           "sidechain.link" },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_extsc_type[] =
        {
            { "Feed-forward",   "sidechain.feed_forward"    },
            { "Feed-back",      "sidechain.feed_back"       },
            { "External",       "sidechain.external"        },
            { "Link",           "sidechain.link" },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_sel_lr[] =
        {
            { "Left",           "dyna_proc.left"            },
            { "Right",          "dyna_proc.right"           },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_sel_ms[] =
        {
            { "Middle",         "dyna_proc.middle"          },
            { "Side",           "dyna_proc.side"            },
            { NULL, NULL }
        };

        static const port_item_t dyna_proc_filter_slope[] =
        {
            { "off",            "eq.slope.off"              },
            { "12 dB/oct",      "eq.slope.12dbo"            },
            { "24 dB/oct",      "eq.slope.24dbo"            },
            { "36 dB/oct",      "eq.slope.36dbo"            },
            { NULL, NULL }
        };

        #define DYNA_PROC_COMMON     \
            BYPASS,             \
            IN_GAIN,            \
            OUT_GAIN,           \
            SWITCH("pause", "Pause graph analysis", 0.0f), \
            TRIGGER("clear", "Clear graph analysis")

        #define DYNA_PROC_LR_COMMON  \
            DYNA_PROC_COMMON,        \
            COMBO("psel", "Processor selector", 0, dyna_proc_sel_lr)

        #define DYNA_PROC_MS_COMMON  \
            DYNA_PROC_COMMON,        \
            COMBO("psel", "Processor selector", 0, dyna_proc_sel_ms), \
            SWITCH("msl", "Mid/Side listen", 0.0f)

        #define DYNA_PROC_SPLIT_COMMON \
            SWITCH("ssplit", "Stereo split", 0.0f), \
            COMBO("sscs", "Split sidechain source", dyna_processor_metadata::SC_SPLIT_SOURCE_DFL, dyna_proc_sc_split_sources)

        #define DYNA_PROC_SHM_LINK_MONO \
            OPT_RETURN_MONO("link", "shml", "Side-chain shared memory link")

        #define DYNA_PROC_SHM_LINK_STEREO \
            OPT_RETURN_STEREO("link", "shml_", "Side-chain shared memory link")

        #define DYNA_PROC_SC_MONO_CHANNEL(sct) \
            COMBO("sct", "Sidechain type", dyna_processor_metadata::SC_TYPE_DFL, sct), \
            COMBO("scm", "Sidechain mode", dyna_processor_metadata::SC_MODE_DFL, dyna_proc_sc_modes), \
            CONTROL("sla", "Sidechain lookahead", U_MSEC, dyna_processor_metadata::LOOKAHEAD), \
            SWITCH("scl", "Sidechain listen", 0.0f), \
            LOG_CONTROL("scr", "Sidechain reactivity", U_MSEC, dyna_processor_metadata::REACTIVITY), \
            AMP_GAIN100("scp", "Sidechain preamp", GAIN_AMP_0_DB), \
            COMBO("shpm", "High-pass filter mode", 0, dyna_proc_filter_slope),      \
            LOG_CONTROL("shpf", "High-pass filter frequency", U_HZ, dyna_processor_metadata::HPF),   \
            COMBO("slpm", "Low-pass filter mode", 0, dyna_proc_filter_slope),      \
            LOG_CONTROL("slpf", "Low-pass filter frequency", U_HZ, dyna_processor_metadata::LPF)

        #define DYNA_PROC_SC_STEREO_CHANNEL(id, label, sct) \
            COMBO("sct" id, "Sidechain type" label, dyna_processor_metadata::SC_TYPE_DFL, sct), \
            COMBO("scm" id, "Sidechain mode" label, dyna_processor_metadata::SC_MODE_DFL, dyna_proc_sc_modes), \
            CONTROL("sla" id, "Sidechain lookahead" label, U_MSEC, dyna_processor_metadata::LOOKAHEAD), \
            SWITCH("scl" id, "Sidechain listen" label, 0.0f), \
            COMBO("scs" id, "Sidechain source" label, dyna_processor_metadata::SC_SOURCE_DFL, dyna_proc_sc_sources), \
            LOG_CONTROL("scr" id, "Sidechain reactivity" label, U_MSEC, dyna_processor_metadata::REACTIVITY), \
            AMP_GAIN100("scp" id, "Sidechain preamp" label, GAIN_AMP_0_DB), \
            COMBO("shpm" id, "High-pass filter mode" label, 0, dyna_proc_filter_slope),      \
            LOG_CONTROL("shpf" id, "High-pass filter frequency" label, U_HZ, dyna_processor_metadata::HPF),   \
            COMBO("slpm" id, "Low-pass filter mode" label, 0, dyna_proc_filter_slope),      \
            LOG_CONTROL("slpf" id, "Low-pass filter frequency" label, U_HZ, dyna_processor_metadata::LPF)

        #define DYNA_POINT(idx, on, id, label, level) \
            SWITCH("pe" #idx id, "Point enable " #idx label, on), \
            LOG_CONTROL_DFL("tl" #idx id, "Threshold " #idx label, U_GAIN_AMP, dyna_processor_metadata::THRESHOLD, level), \
            LOG_CONTROL_DFL("gl" #idx id, "Gain " #idx label, U_GAIN_AMP, dyna_processor_metadata::THRESHOLD, level), \
            LOG_CONTROL("kn" #idx id, "Knee " #idx label, U_GAIN_AMP, dyna_processor_metadata::KNEE), \
            SWITCH("ae" #idx id, "Attack enable " #idx label, 0.0f), \
            LOG_CONTROL_DFL("al" #idx id, "Attack level " #idx label, U_GAIN_AMP, dyna_processor_metadata::ATTACK_LVL, level), \
            LOG_CONTROL("at" #idx id, "Attack time " #idx label, U_MSEC, dyna_processor_metadata::ATTACK_TIME), \
            SWITCH("re" #idx id, "Release enable " #idx label, 0.0f), \
            LOG_CONTROL_DFL("rl" #idx id, "Relative level " #idx label, U_GAIN_AMP, dyna_processor_metadata::RELEASE_LVL, level), \
            LOG_CONTROL("rt" #idx id, "Release time " #idx label, U_MSEC, dyna_processor_metadata::RELEASE_TIME)

        #define DYNA_PROC_CHANNEL(id, label) \
            LOG_CONTROL("atd" id, "Attack time default" label, U_MSEC, dyna_processor_metadata::ATTACK_TIME), \
            LOG_CONTROL("rtd" id, "Release time default" label, U_MSEC, dyna_processor_metadata::RELEASE_TIME), \
            DYNA_POINT(0, 1.0f, id, label, GAIN_AMP_M_12_DB), \
            DYNA_POINT(1, 0.0f, id, label, GAIN_AMP_M_24_DB), \
            DYNA_POINT(2, 0.0f, id, label, GAIN_AMP_M_36_DB), \
            DYNA_POINT(3, 0.0f, id, label, GAIN_AMP_M_48_DB), \
            CONTROL("hold" id, "Hold time" label, U_MSEC, dyna_processor_metadata::HOLD_TIME), \
            LOG_CONTROL("llr" id, "Low-level ratio" label, U_NONE, dyna_processor_metadata::RATIO), \
            LOG_CONTROL("hlr" id, "High-level ratio" label, U_NONE, dyna_processor_metadata::RATIO), \
            LOG_CONTROL("omk" id, "Overall makeup gain" label, U_GAIN_AMP, dyna_processor_metadata::MAKEUP), \
            AMP_GAIN10("cdr" id, "Dry gain" label, GAIN_AMP_M_INF_DB),     \
            AMP_GAIN10("cwt" id, "Wet gain" label, GAIN_AMP_0_DB), \
            PERCENTS("cdw" id, "Dry/Wet balance" label, 100.0f, 0.1f), \
            SWITCH("cmv" id, "Curve modelling visibility" label, 1.0f), \
            MESH("cmg" id, "Curve modelling graph" label, 2, dyna_processor_metadata::CURVE_MESH_SIZE), \
            MESH("ccg" id, "Curve graph" label, 2, dyna_processor_metadata::CURVE_MESH_SIZE)

        #define DYNA_PROC_AUDIO_METER(id, label) \
            SWITCH("slv" id, "Sidechain level visibility" label, 1.0f), \
            SWITCH("elv" id, "Envelope level visibility" label, 1.0f), \
            SWITCH("grv" id, "Gain reduction visibility" label, 1.0f), \
            SWITCH("ilv" id, "Input level visibility" label, 1.0f), \
            SWITCH("olv" id, "Output level visibility" label, 1.0f), \
            MESH("scg" id, "Sidechain graph" label, 2, dyna_processor_metadata::TIME_MESH_SIZE), \
            MESH("evg" id, "Envelope graph" label, 2, dyna_processor_metadata::TIME_MESH_SIZE), \
            MESH("grg" id, "Gain reduciton" label, 2, dyna_processor_metadata::TIME_MESH_SIZE + 4), \
            MESH("isg" id, "Input signal graph" label, 2, dyna_processor_metadata::TIME_MESH_SIZE + 2), \
            MESH("osg" id, "Output signal graph" label, 2, dyna_processor_metadata::TIME_MESH_SIZE), \
            METER_OUT_GAIN("slm" id, "Sidechain level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("clm" id, "Curve level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("elm" id, "Envelope level meter" label, GAIN_AMP_P_36_DB), \
            METER_GAIN_DFL("rlm" id, "Reduction level meter" label, GAIN_AMP_P_60_DB, GAIN_AMP_0_DB), \
            METER_GAIN("ilm" id, "Input level meter" label, GAIN_AMP_P_36_DB), \
            METER_GAIN("olm" id, "Output level meter" label, GAIN_AMP_P_36_DB)

        static const port_t dyna_processor_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            DYNA_PROC_SHM_LINK_MONO,
            DYNA_PROC_COMMON,
            DYNA_PROC_SC_MONO_CHANNEL(dyna_proc_sc_type),
            DYNA_PROC_CHANNEL("", ""),
            DYNA_PROC_AUDIO_METER("", ""),

            PORTS_END
        };

        static const port_t dyna_processor_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            DYNA_PROC_SHM_LINK_STEREO,
            DYNA_PROC_COMMON,
            DYNA_PROC_SPLIT_COMMON,
            DYNA_PROC_SC_STEREO_CHANNEL("", "", dyna_proc_sc_type),
            DYNA_PROC_CHANNEL("", ""),
            DYNA_PROC_AUDIO_METER("_l", " Left"),
            DYNA_PROC_AUDIO_METER("_r", " Right"),

            PORTS_END
        };

        static const port_t dyna_processor_lr_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            DYNA_PROC_SHM_LINK_STEREO,
            DYNA_PROC_LR_COMMON,
            DYNA_PROC_SC_STEREO_CHANNEL("_l", " Left", dyna_proc_sc_type),
            DYNA_PROC_SC_STEREO_CHANNEL("_r", " Right", dyna_proc_sc_type),
            DYNA_PROC_CHANNEL("_l", " Left"),
            DYNA_PROC_CHANNEL("_r", " Right"),
            DYNA_PROC_AUDIO_METER("_l", " Left"),
            DYNA_PROC_AUDIO_METER("_r", " Right"),

            PORTS_END
        };

        static const port_t dyna_processor_ms_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            DYNA_PROC_SHM_LINK_STEREO,
            DYNA_PROC_MS_COMMON,
            DYNA_PROC_SC_STEREO_CHANNEL("_m", " Mid", dyna_proc_sc_type),
            DYNA_PROC_SC_STEREO_CHANNEL("_s", " Side", dyna_proc_sc_type),
            DYNA_PROC_CHANNEL("_m", " Mid"),
            DYNA_PROC_CHANNEL("_s", " Side"),
            DYNA_PROC_AUDIO_METER("_m", " Mid"),
            DYNA_PROC_AUDIO_METER("_s", " Side"),

            PORTS_END
        };

        static const port_t sc_dyna_processor_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            PORTS_MONO_SIDECHAIN,
            DYNA_PROC_SHM_LINK_MONO,
            DYNA_PROC_COMMON,
            DYNA_PROC_SC_MONO_CHANNEL(dyna_proc_extsc_type),
            DYNA_PROC_CHANNEL("", ""),
            DYNA_PROC_AUDIO_METER("", ""),

            PORTS_END
        };

        static const port_t sc_dyna_processor_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            DYNA_PROC_SHM_LINK_STEREO,
            DYNA_PROC_COMMON,
            DYNA_PROC_SPLIT_COMMON,
            DYNA_PROC_SC_STEREO_CHANNEL("", "", dyna_proc_extsc_type),
            DYNA_PROC_CHANNEL("", ""),
            DYNA_PROC_AUDIO_METER("_l", " Left"),
            DYNA_PROC_AUDIO_METER("_r", " Right"),

            PORTS_END
        };

        static const port_t sc_dyna_processor_lr_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            DYNA_PROC_SHM_LINK_STEREO,
            DYNA_PROC_LR_COMMON,
            DYNA_PROC_SC_STEREO_CHANNEL("_l", " Left", dyna_proc_extsc_type),
            DYNA_PROC_SC_STEREO_CHANNEL("_r", " Right", dyna_proc_extsc_type),
            DYNA_PROC_CHANNEL("_l", " Left"),
            DYNA_PROC_CHANNEL("_r", " Right"),
            DYNA_PROC_AUDIO_METER("_l", " Left"),
            DYNA_PROC_AUDIO_METER("_r", " Right"),

            PORTS_END
        };

        static const port_t sc_dyna_processor_ms_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            DYNA_PROC_SHM_LINK_STEREO,
            DYNA_PROC_MS_COMMON,
            DYNA_PROC_SC_STEREO_CHANNEL("_m", " Mid", dyna_proc_extsc_type),
            DYNA_PROC_SC_STEREO_CHANNEL("_s", " Side", dyna_proc_extsc_type),
            DYNA_PROC_CHANNEL("_m", " Mid"),
            DYNA_PROC_CHANNEL("_s", " Side"),
            DYNA_PROC_AUDIO_METER("_m", " Mid"),
            DYNA_PROC_AUDIO_METER("_s", " Side"),

            PORTS_END
        };

        const meta::bundle_t dyna_processor_bundle =
        {
            "dyna_processor",
            "Dynamics Processor",
            B_DYNAMICS,
            "rvEhP_PRtzk",
            "This plugin performs dynamics processing of input signal. Flexible\nsidechain-control configuration provided. There are possible different\nvariants of dynamics processor to build: compressor, limiter, gate,\nexpander, some kind of transient designer and many others due to\npossibility of flexible dynamic curve configuration. Also dynamic\nrange can be split into sub-ranges that may have their individual\nattack and release timings."
        };

        // Dynamics Processor
        const meta::plugin_t  dyna_processor_mono =
        {
            "Dynamikprozessor Mono",
            "Dynamics Processor Mono",
            "Dynamics Processor Mono",
            "DP1M",
            &developers::v_sadovnikov,
            "dyna_processor_mono",
            {
                LSP_LV2_URI("dyna_processor_mono"),
                LSP_LV2UI_URI("dyna_processor_mono"),
                "lqpm",
                LSP_VST3_UID("dp1m    lqpm"),
                LSP_VST3UI_UID("dp1m    lqpm"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 0,
                LSP_LADSPA_URI("dyna_processor_mono"),
                LSP_CLAP_URI("dyna_processor_mono"),
                LSP_GST_UID("dyna_processor_mono"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_mono,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            dyna_processor_mono_ports,
            "dynamics/processor/single/mono.xml",
            NULL,
            mono_plugin_port_groups,
            &dyna_processor_bundle
        };

        const meta::plugin_t  dyna_processor_stereo =
        {
            "Dynamikprozessor Stereo",
            "Dynamics Processor Stereo",
            "Dynamics Processor Stereo",
            "DP1S",
            &developers::v_sadovnikov,
            "dyna_processor_stereo",
            {
                LSP_LV2_URI("dyna_processor_stereo"),
                LSP_LV2UI_URI("dyna_processor_stereo"),
                "aat9",
                LSP_VST3_UID("dp1s    aat9"),
                LSP_VST3UI_UID("dp1s    aat9"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 1,
                LSP_LADSPA_URI("dyna_processor_stereo"),
                LSP_CLAP_URI("dyna_processor_stereo"),
                LSP_GST_UID("dyna_processor_stereo"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            dyna_processor_stereo_ports,
            "dynamics/processor/single/stereo.xml",
            NULL,
            stereo_plugin_port_groups,
            &dyna_processor_bundle
        };

        const meta::plugin_t  dyna_processor_lr =
        {
            "Dynamikprozessor LeftRight",
            "Dynamics Processor LeftRight",
            "Dynamics Processor L/R",
            "DP1LR",
            &developers::v_sadovnikov,
            "dyna_processor_lr",
            {
                LSP_LV2_URI("dyna_processor_lr"),
                LSP_LV2UI_URI("dyna_processor_lr"),
                "hl9g",
                LSP_VST3_UID("dp1lr   hl9g"),
                LSP_VST3UI_UID("dp1lr   hl9g"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 2,
                LSP_LADSPA_URI("dyna_processor_lr"),
                LSP_CLAP_URI("dyna_processor_lr"),
                LSP_GST_UID("dyna_processor_lr"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            dyna_processor_lr_ports,
            "dynamics/processor/single/lr.xml",
            NULL,
            stereo_plugin_port_groups,
            &dyna_processor_bundle
        };

        const meta::plugin_t  dyna_processor_ms =
        {
            "Dynamikprozessor MidSide",
            "Dynamics Processor MidSide",
            "Dynamics Processor M/S",
            "DP1MS",
            &developers::v_sadovnikov,
            "dyna_processor_ms",
            {
                LSP_LV2_URI("dyna_processor_ms"),
                LSP_LV2UI_URI("dyna_processor_ms"),
                "uvrg",
                LSP_VST3_UID("dp1ms   uvrg"),
                LSP_VST3UI_UID("dp1ms   uvrg"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 3,
                LSP_LADSPA_URI("dyna_processor_ms"),
                LSP_CLAP_URI("dyna_processor_ms"),
                LSP_GST_UID("dyna_processor_ms"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            dyna_processor_ms_ports,
            "dynamics/processor/single/ms.xml",
            NULL,
            stereo_plugin_port_groups,
            &dyna_processor_bundle
        };

        // Sidechain compressor
        const meta::plugin_t  sc_dyna_processor_mono =
        {
            "Sidechain-Dynamikprozessor Mono",
            "Sidechain Dynamics Processor Mono",
            "SC Dynamics Processor Mono",
            "SCDP1M",
            &developers::v_sadovnikov,
            "sc_dyna_processor_mono",
            {
                LSP_LV2_URI("sc_dyna_processor_mono"),
                LSP_LV2UI_URI("sc_dyna_processor_mono"),
                "apkx",
                LSP_VST3_UID("scdp1m  apkx"),
                LSP_VST3UI_UID("scdp1m  apkx"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 4,
                LSP_LADSPA_URI("sc_dyna_processor_mono"),
                LSP_CLAP_URI("sc_dyna_processor_mono"),
                LSP_GST_UID("sc_dyna_processor_mono"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_mono,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_dyna_processor_mono_ports,
            "dynamics/processor/single/mono.xml",
            NULL,
            mono_plugin_sidechain_port_groups,
            &dyna_processor_bundle
        };

        const meta::plugin_t  sc_dyna_processor_stereo =
        {
            "Sidechain-Dynamikprozessor Stereo",
            "Sidechain Dynamics Processor Stereo",
            "SC Dynamics Processor Stereo",
            "SCDP1S",
            &developers::v_sadovnikov,
            "sc_dyna_processor_stereo",
            {
                LSP_LV2_URI("sc_dyna_processor_stereo"),
                LSP_LV2UI_URI("sc_dyna_processor_stereo"),
                "fqne",
                LSP_VST3_UID("scdp1s  fqne"),
                LSP_VST3UI_UID("scdp1s  fqne"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 5,
                LSP_LADSPA_URI("sc_dyna_processor_stereo"),
                LSP_CLAP_URI("sc_dyna_processor_stereo"),
                LSP_GST_UID("sc_dyna_processor_stereo"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_dyna_processor_stereo_ports,
            "dynamics/processor/single/stereo.xml",
            NULL,
            stereo_plugin_sidechain_port_groups,
            &dyna_processor_bundle
        };

        const meta::plugin_t  sc_dyna_processor_lr =
        {
            "Sidechain-Dynamikprozessor LeftRight",
            "Sidechain Dynamics Processor LeftRight",
            "SC Dynamics Processor L/R",
            "SCDP1LR",
            &developers::v_sadovnikov,
            "sc_dyna_processor_lr",
            {
                LSP_LV2_URI("sc_dyna_processor_lr"),
                LSP_LV2UI_URI("sc_dyna_processor_lr"),
                "sxmi",
                LSP_VST3_UID("scdp1lr sxmi"),
                LSP_VST3UI_UID("scdp1lr sxmi"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 6,
                LSP_LADSPA_URI("sc_dyna_processor_lr"),
                LSP_CLAP_URI("sc_dyna_processor_lr"),
                LSP_GST_UID("sc_dyna_processor_lr"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_dyna_processor_lr_ports,
            "dynamics/processor/single/lr.xml",
            NULL,
            stereo_plugin_sidechain_port_groups,
            &dyna_processor_bundle
        };

        const meta::plugin_t  sc_dyna_processor_ms =
        {
            "Sidechain-Dynamikprozessor MidSide",
            "Sidechain Dynamics Processor MidSide",
            "SC Dynamics Processor M/S",
            "SCDP1MS",
            &developers::v_sadovnikov,
            "sc_dyna_processor_ms",
            {
                LSP_LV2_URI("sc_dyna_processor_ms"),
                LSP_LV2UI_URI("sc_dyna_processor_ms"),
                "fcj9",
                LSP_VST3_UID("scdp1ms fcj9"),
                LSP_VST3UI_UID("scdp1ms fcj9"),
                LSP_LADSPA_DYNAMIC_PROCESSOR_BASE + 7,
                LSP_LADSPA_URI("sc_dyna_processor_ms"),
                LSP_CLAP_URI("sc_dyna_processor_ms"),
                LSP_GST_UID("sc_dyna_processor_ms"),
            },
            LSP_PLUGINS_DYNA_PROCESSOR_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_dyna_processor_ms_ports,
            "dynamics/processor/single/ms.xml",
            NULL,
            stereo_plugin_sidechain_port_groups,
            &dyna_processor_bundle
        };
    } /* namespace meta */
} /* namespace lsp */

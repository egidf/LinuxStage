#include "TestUtil.h"

#include "plugins/IPluginHost.h"

TEST_CASE(test_is_lsp_plugin_descriptor) {
    lspa::PluginDescriptor lsp;
    lsp.name = "LSP Parametric EQ";
    lsp.label = "ParaEQ";
    lsp.author = "LSP plugins";
    lsp.uri = "http://lsp-plug.in/plugins/lv2/para_eq_x16_stereo";
    CHECK(lspa::isLspPluginDescriptor(lsp));

    lspa::PluginDescriptor other;
    other.name = "SWH Limiter";
    other.label = "Limiter";
    other.author = "Steve Harris";
    other.uri = "http://plugin.org.uk/lv2/limiter";
    CHECK(!lspa::isLspPluginDescriptor(other));

    lspa::PluginDescriptor mixed;
    mixed.name = "Some Other plugin";
    mixed.label = "LSPClone";
    mixed.author = "Third party";
    mixed.uri = "urn:example:demo";
    CHECK(lspa::isLspPluginDescriptor(mixed));
}

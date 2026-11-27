// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/BoundedDataInput.h"
#include "../src/PortableWorkspace.h"
#include <cassert>
#include <iostream>
#include <limits>
using Input=armix::storage::BoundedDataInput;
using Workspace=armix::storage::PortableWorkspace;
int main() {
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixAdversarial","",false);
    assert(root.createDirectory().wasOk());
    const auto file=root.getChildFile("fixture.json");
    juce::String text="preserved";
    const auto invalid=[&](std::initializer_list<unsigned char> bytes) {
        juce::MemoryBlock mb(bytes.begin(),bytes.size());
        assert(!Input::decodeUtf8(mb,text));
        assert(text=="preserved");
        assert(file.replaceWithData(mb.getData(),mb.getSize()));
        assert(!Input::text(file,1024,text)&&text=="preserved");
    };
    invalid({0x7b,0x7d,0,0x78});
    invalid({0xc0,0xaf});
    invalid({0xed,0xa0,0x80});
    invalid({0xf4,0x90,0x80,0x80});
    invalid({0xe2,0x82});
    invalid({0x80});
    invalid({0xef,0xbb,0xbf});
    const unsigned char good[]{0xef,0xbb,0xbf,0x7b,0x7d};
    assert(Input::decodeUtf8(juce::MemoryBlock(good,sizeof good),text)&&text=="{}");
    assert(file.replaceWithText(juce::String::fromUTF8("{\"label\":\"日本語 🎵\"}")));
    assert(Input::text(file,1024,text)&&Input::jsonStructure(text));
    juce::MemoryBlock data("safe",4);
    assert(!Input::read(file,1,data)&&data==juce::MemoryBlock("safe",4));
    assert(!Input::read(file,std::numeric_limits<juce::int64>::max(),data));
    assert(Input::read(file,1024,data));
    assert(file.replaceWithText(juce::String::repeatedString("[",64)+juce::String::repeatedString("]",64)));
    assert(Input::text(file,1024,text)&&Input::jsonStructure(text));
    assert(!Input::jsonStructure("["+text+"]"));
    assert(!Input::jsonStructure("{]"));
    // Reproduce the actual UBSan failure without invoking the unsafe parser.
    for(const auto* invalidNumber:{"[444444444444444444444444444]",
         "[9223372036854775808]","[-9223372036854775808]",
         "[9223372036854775808.0]","[1e40666666666]","[1e-2147483648]",
         "[1e+]","[- 1]","[01]","[1.]"}) {
        assert(!Input::jsonStructure(invalidNumber));
    }
    for(const auto* validNumber:{"[9223372036854775807]","[-9223372036854775807]",
         "[0,-0,0.5,-1.25e-4,1e+308,5e-324]",
         "{\"id\":\"18446744073709551615\",\"label\":\"1e40666666666\"}"}) {
        assert(Input::jsonStructure(validNumber));
        assert(!juce::JSON::parse(validNumber).isVoid());
    }
    // Reproducer from UBSan on JUCE parseNumber, plus the exponent path.
    for(const auto* bad:{"{\"n\":[444444444444444444444444444]}",
                         "{\"n\":9223372036854775808}",
                         "{\"n\":-9223372036854775808}",
                         "{\"n\":9223372036854775808.0}",
                         "{\"n\":1e2147483648}",
                         "{\"n\":1e-9999999999999999999}",
                         "{\"n\":- 444444444444444444444444444}",
                         "{\"n\":1e+}", "{\"n\":1.}"})
        assert(!Input::jsonStructure(bad));
    for(const auto* good:{"{\"n\":9223372036854775807}",
                          "{\"n\":-9223372036854775807}",
                          "{\"n\":0.12345678901234567890123456789}",
                          "{\"n\":1.25e-120}",
                          "{\"n\":0e+000000000000000000001}",
                          "{\"id\":\"18446744073709551615\"}"}) {
        assert(Input::jsonStructure(good));
        assert(juce::JSON::parse(good).isObject());
    }
    assert(!Input::jsonStructure("[1e1000]"));
    assert(!Input::jsonStructure("[1e-1000]"));
    assert(!Input::jsonStructure("{'n':1}"));
    assert(!Input::jsonStructure("[1true]"));
    assert(Input::jsonStructure("{\"label\":\"it's safe\"}"));
    assert(!Input::jsonStructure("{\"a\":\"unterminated}"));
    assert(Input::jsonStructure("{\"a\":\"[{}]\\\"\"}"));
    for(const auto* path:{"../escape","a/../escape","a//b","/a","a/",
                          "a\\b","a:stream","CON.txt","x/aux.vst3","x/LPT1",
                          "x/COM¹.txt","x/alias. ","x/bad?name","x/bad\nname"}) {
        const auto decodedPath=juce::String::fromUTF8(path);
        if(Workspace::safeRelativePath(decodedPath))std::cerr<<"Unexpected admitted path: "<<path<<"\n";
        assert(!Workspace::safeRelativePath(decodedPath));
        assert(Workspace::decode(root,juce::String("workspace://")+decodedPath)==juce::File());
    }
    assert(Workspace::safeRelativePath("Plugins/VST3/My Reverb.vst3"));
    assert(Workspace::decode(root,"workspace://Plugins/VST3/My Reverb.vst3").isAChildOf(root));
#if JUCE_LINUX || JUCE_MAC
    const auto external=root.getSiblingFile(root.getFileName()+"-outside");
    assert(external.createDirectory().wasOk());
    const auto externalFile=external.getChildFile("protected.txt");
    assert(externalFile.replaceWithText("DO NOT MODIFY"));
    const auto link=root.getChildFile("linked");
    assert(external.createSymbolicLink(link,false));
    assert(!Input::managedPath(root,link.getChildFile("protected.txt")));
    assert(Workspace::decode(root,"workspace://linked/protected.txt")==juce::File());
    assert(externalFile.loadFileAsString()=="DO NOT MODIFY");
    assert(link.deleteFile());
    assert(external.deleteRecursively());
#endif
    assert(root.deleteRecursively());
    std::cout<<"ZBSH10_ADVERSARIAL_INPUT_PASS\n";
}

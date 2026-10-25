// Original OpenGOAL background exporter with explicit source LODs (AI-assisted), ISC.
// Host-only: input is the owner's validated decompressed version-43 FR3.
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include "common/custom_data/Tfrag3Data.h"
#include "decompiler/level_extractor/fr3_to_gltf.h"

// Standalone helper: retain serializer assertions without the game/logging runtime.
[[noreturn]] void private_assert_failed(const char* e,const char* f,int l,const char*,const char* m){
 throw std::runtime_error(std::string(f)+":"+std::to_string(l)+" "+e+" "+m);
}
[[noreturn]] void private_assert_failed(const char* e,const char* f,int l,const char* n,const std::string_view& m){
 private_assert_failed(e,f,l,n,std::string(m).c_str());
}
namespace lg {namespace internal {void log_print(const char* s){std::cout<<s;}}}
// Needed by the linked foreground-export object; background export does not use it.
namespace file_util {bool create_dir_if_needed_for_file(const fs::path& p){
 return !p.parent_path().empty() && fs::create_directories(p.parent_path());
}}
static unsigned lod(const char* text,unsigned cap){
 unsigned value=0;std::string_view s(text);auto r=std::from_chars(s.data(),s.data()+s.size(),value);
 if(r.ec!=std::errc{} || r.ptr!=s.data()+s.size() || value>=cap)throw std::runtime_error("invalid source LOD");
 return value;
}
static uint32_t read_le32(const unsigned char* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
int main(int argc,char** argv){fs::path temporary;bool owns_temporary=false;try{
 if(argc!=5){std::cerr<<"usage: export-background decompressed.fr3 output.glb tfrag-lod(0..2) tie-lod(0..3)\n";return 2;}
 const unsigned tfrag=lod(argv[3],tfrag3::TFRAG_GEOS),tie=lod(argv[4],tfrag3::TIE_GEOS);
 const fs::path output=fs::absolute(fs::path(argv[2])).lexically_normal();
 temporary=fs::path(output.string()+".partial");
 if(temporary.parent_path()!=output.parent_path() || !fs::is_directory(output.parent_path()))throw std::runtime_error("invalid output parent");
 if(fs::exists(output) || fs::exists(temporary))throw std::runtime_error("output and partial must be new");
 const auto size=fs::file_size(argv[1]);if(!size || size>256u*1024u*1024u)throw std::runtime_error("raw input size");
 std::ifstream in(argv[1],std::ios::binary);std::vector<u8> bytes((std::istreambuf_iterator<char>(in)),{});
 if(bytes.size()!=size)throw std::runtime_error("raw input read failed");
 Serializer reader(bytes.data(),bytes.size());tfrag3::Level level;level.serialize(reader);
 if(!reader.get_load_finished() || level.version!=43 || level.version2!=43)throw std::runtime_error("version or trailing serialized bytes");
 if(level.tfrag_trees[tfrag].empty() || level.tie_trees[tie].empty())throw std::runtime_error("requested source LOD absent");
 if(tfrag)std::swap(level.tfrag_trees[0],level.tfrag_trees[tfrag]);
 if(tie)std::swap(level.tie_trees[0],level.tie_trees[tie]);
 owns_temporary=true;save_level_background_as_gltf(level,temporary);
 unsigned char header[12];std::ifstream check(temporary.string(),std::ios::binary);
 if(!check.read(reinterpret_cast<char*>(header),12) || read_le32(header)!=0x46546c67u || read_le32(header+4)!=2 || read_le32(header+8)!=fs::file_size(temporary))
  throw std::runtime_error("incomplete exported GLB");
 check.close();if(fs::exists(output))throw std::runtime_error("output appeared during export");
 fs::rename(temporary,output);owns_temporary=false;
 std::cout<<"version=43 tfrag_lod="<<tfrag<<" tie_lod="<<tie<<" serializer_full_consumption=PASS\n";
 }catch(const std::exception& e){if(owns_temporary){std::error_code ignored;fs::remove(temporary,ignored);}std::cerr<<e.what()<<"\n";return 1;}}

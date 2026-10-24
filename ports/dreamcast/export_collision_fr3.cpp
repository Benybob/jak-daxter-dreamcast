// Read original version-43 FR3 through OpenGOAL's serializer (AI-assisted).
// Host-only helper. Input is the decompressed FR3 payload, not a PS2 file.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include "common/custom_data/Tfrag3Data.h"

// Keep the serializer's checks enabled without linking the game/logging runtime.
[[noreturn]] void private_assert_failed(const char* e,const char* f,int l,const char*,const char* m){
 throw std::runtime_error(std::string(f)+":"+std::to_string(l)+" "+e+" "+m);
}
[[noreturn]] void private_assert_failed(const char* e,const char* f,int l,const char* n,const std::string_view& m){
 private_assert_failed(e,f,l,n,std::string(m).c_str());
}
namespace lg {namespace internal {void log_print(const char* s){std::cout<<s;}}}

int main(int argc,char** argv){try{
 if(argc!=3){std::cerr<<"usage: export-collision decompressed.fr3 output.dcr\n";return 2;}
 std::ifstream in(argv[1],std::ios::binary);
 std::vector<u8> bytes((std::istreambuf_iterator<char>(in)),{});
 if(bytes.empty() || bytes.size()>256u*1024u*1024u)throw std::runtime_error("raw input size");
 Serializer reader(bytes.data(),bytes.size());tfrag3::Level level;level.serialize(reader);
 if(!reader.get_load_finished())throw std::runtime_error("trailing serialized bytes");
 const auto& vertices=level.collision.vertices;
 if(vertices.empty() || vertices.size()%3 || vertices.size()>2000000)throw std::runtime_error("collision count");
 struct Face {float xyz[9];uint32_t pat;};static_assert(sizeof(Face)==40);
 std::vector<Face> faces;uint32_t degenerate=0;
 for(size_t i=0;i<vertices.size();i+=3){Face f{};f.pat=vertices[i].pat;
  for(unsigned j=0;j<3;++j){
   if(vertices[i+j].pat!=f.pat)throw std::runtime_error("inconsistent face PAT");
   f.xyz[j*3]=vertices[i+j].x/4096;f.xyz[j*3+1]=vertices[i+j].y/4096;f.xyz[j*3+2]=vertices[i+j].z/4096;
  }
  for(float x:f.xyz)if(!std::isfinite(x))throw std::runtime_error("nonfinite collision position");
  auto a=math::Vector3f(f.xyz[3]-f.xyz[0],f.xyz[4]-f.xyz[1],f.xyz[5]-f.xyz[2]);
  auto b=math::Vector3f(f.xyz[6]-f.xyz[0],f.xyz[7]-f.xyz[1],f.xyz[8]-f.xyz[2]);
  auto n=a.cross(b);if(n.dot(n)<1e-14f){++degenerate;continue;}
  faces.push_back(f);
 }
 const uint32_t header[3]={1,tfrag3::TFRAG3_VERSION,(uint32_t)faces.size()};
 std::ofstream out(argv[2],std::ios::binary);out.write("DCR1",4);out.write((const char*)header,sizeof header);
 out.write((const char*)faces.data(),faces.size()*sizeof(Face));
 if(!out)throw std::runtime_error("output write failed");
 std::cout<<"level="<<level.level_name<<" version="<<level.version<<" triangles="<<faces.size()
          <<" degenerate_skipped="<<degenerate<<" serializer_full_consumption=PASS\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}

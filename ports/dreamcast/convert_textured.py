"""Static GLB mesh to indexed Dreamcast geometry and twiddled textures (AI-assisted).

DCT1 is a bounded inspection format, not a game level/streaming format. Supports
embedded PNG/JPEG textures, vertex colors, base-color factors, wrapping and alpha
masking. BLEND, skins, morph targets and texture transforms fail explicitly.
"""
import argparse
import base64
from io import BytesIO
import json
import math
from pathlib import Path
import struct
from PIL import Image
from convert_scene import read_glb, accessor, multiply, transform, node_matrix, IDENTITY

MAX_TRIANGLES=8192
MAX_VERTICES=24576
MAX_TEXTURES=64
MAX_TEXTURE_BYTES=2*1024*1024

def twiddle(x,y,width,height):
    side=min(width,height)
    mask=side-1
    value=0
    for bit in range(side.bit_length()-1):
        value|=((y>>bit)&1)<<(bit*2)
        value|=((x>>bit)&1)<<(bit*2+1)
    return value+((x&~mask)+(y&~mask))*side

def texture_pixels(doc,binary,image_id,mode,cutoff,size):
    if image_id is None:
        image=Image.new('RGBA',(8,8),(255,255,255,255))
    else:
        info=doc['images'][image_id]
        if 'bufferView' in info:
            view=doc['bufferViews'][info['bufferView']]
            start=view.get('byteOffset',0)
            raw=binary[start:start+view['byteLength']]
        elif info.get('uri','').startswith(('data:image/png;base64,','data:image/jpeg;base64,')):
            raw=base64.b64decode(info['uri'].split(',',1)[1],validate=True)
        else:
            raise ValueError('only embedded texture images are supported')
        image=Image.open(BytesIO(raw))
        if max(image.size)>4096: raise ValueError('source image too large')
        image=image.convert('RGBA')
    dims=tuple(max(8,min(size,1<<(value.bit_length()-1))) for value in image.size)
    image=image.resize(dims,Image.Resampling.LANCZOS)
    width,height=dims
    result=bytearray(width*height*2)
    pixels=image.load()
    for y in range(height):
        for x in range(width):
            r,g,b,a=pixels[x,y]
            opaque=mode=='OPAQUE' or a/255>=cutoff
            value=(int(opaque)<<15)|((r>>3)<<10)|((g>>3)<<5)|(b>>3)
            struct.pack_into('<H',result,twiddle(x,y,width,height)*2,value)
    return width,height,bytes(result)

def convert(doc,binary,mesh_index,size=128,texture_format='argb1555'):
    if not 0<=mesh_index<len(doc['meshes']): raise ValueError('mesh index outside mesh array')
    if size<8 or size>512 or size&(size-1): raise ValueError('texture size must be a power of two from 8 to 512')
    if texture_format not in ('argb1555','vq'): raise ValueError('unsupported native texture format')
    vertices,indices,batches,textures=[],[],[],[]
    vertex_map,texture_map={},{}
    needed=set()
    nodes=doc['nodes']
    roots=doc['scenes'][doc.get('scene',0)]['nodes']
    def select(index,ancestors):
        if index in ancestors: raise ValueError('cycle in node graph')
        node=nodes[index]
        wanted=node.get('mesh')==mesh_index
        for child in node.get('children',[]): wanted=select(child,ancestors|{index}) or wanted
        if wanted: needed.add(index)
        return wanted
    for root in roots: select(root,set())
    def walk(index,parent):
        if index not in needed: return
        node=nodes[index]
        matrix=multiply(parent,node_matrix(node))
        if 'skin' in node: raise ValueError('skinned geometry unsupported')
        if node.get('mesh')==mesh_index:
            for primitive in doc['meshes'][mesh_index]['primitives']:
                if primitive.get('mode',4)!=4 or 'targets' in primitive: raise ValueError('static TRIANGLES required')
                attrs=primitive['attributes']
                positions=accessor(doc,binary,attrs['POSITION'])
                uvs=accessor(doc,binary,attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else [(0,0)]*len(positions)
                colors=accessor(doc,binary,attrs['COLOR_0']) if 'COLOR_0' in attrs else [(1,1,1,1)]*len(positions)
                if len(uvs)!=len(positions) or len(colors)!=len(positions): raise ValueError('attribute count mismatch')
                selected=accessor(doc,binary,primitive['indices']) if 'indices' in primitive else [(i,) for i in range(len(positions))]
                if len(selected)%3: raise ValueError('incomplete triangle')
                material=doc.get('materials',[])[primitive['material']] if 'material' in primitive else {}
                mode=material.get('alphaMode','OPAQUE')
                if mode not in ('OPAQUE','MASK'): raise ValueError('BLEND requires a separate translucent path')
                cutoff=material.get('alphaCutoff',0.5)
                if not math.isfinite(cutoff) or not 0<=cutoff<=1: raise ValueError('invalid alpha cutoff')
                pbr=material.get('pbrMetallicRoughness',{})
                factor=pbr.get('baseColorFactor',[1,1,1,1])
                if len(factor)!=4 or not all(math.isfinite(c) for c in factor): raise ValueError('invalid base color')
                texture=pbr.get('baseColorTexture')
                image_id=None
                sampler={}
                if texture:
                    if texture.get('texCoord',0)!=0 or texture.get('extensions'): raise ValueError('unsupported texture coordinates/transform')
                    tex=doc['textures'][texture['index']]
                    image_id=tex['source']
                    if 'sampler' in tex: sampler=doc['samplers'][tex['sampler']]
                key=(image_id,mode,cutoff)
                if key not in texture_map:
                    texture_map[key]=len(textures)
                    textures.append(texture_pixels(doc,binary,image_id,mode,cutoff,size))
                flags=int(mode=='MASK')
                for axis,clamp,mirror in [('S',4,16),('T',8,32)]:
                    wrap=sampler.get('wrap'+axis,10497)
                    if wrap==33071: flags|=clamp
                    elif wrap==33648: flags|=mirror
                    elif wrap!=10497: raise ValueError('unsupported sampler wrap')
                first=len(indices)
                for entry in selected:
                    if len(entry)!=1 or not isinstance(entry[0],int) or not 0<=entry[0]<len(positions): raise ValueError('invalid index')
                    i=entry[0]
                    if len(positions[i])!=3 or len(uvs[i])!=2 or len(colors[i]) not in (3,4): raise ValueError('invalid vertex attribute width')
                    point=transform(matrix,positions[i])
                    if not all(math.isfinite(v) for v in point) or any(abs(v)>65536 for v in uvs[i]): raise ValueError('invalid position/UV')
                    color=list(colors[i])+[1]*(4-len(colors[i]))
                    rgba=[round(max(0,min(1,color[k]*factor[k]))*255) for k in range(4)]
                    packed=rgba[3]<<24|rgba[0]<<16|rgba[1]<<8|rgba[2]
                    vertex=tuple(point)+tuple(uvs[i])+(packed,)
                    if vertex not in vertex_map:
                        vertex_map[vertex]=len(vertices)
                        vertices.append(vertex)
                    indices.append(vertex_map[vertex])
                batches.append((first,len(indices)-first,texture_map[key],flags))
        for child in node.get('children',[]): walk(child,matrix)
    for root in roots: walk(root,IDENTITY)
    if not vertices or not indices: raise ValueError('no selected triangles')
    if len(indices)>MAX_TRIANGLES*3 or len(vertices)>MAX_VERTICES or len(batches)>128 or len(textures)>MAX_TEXTURES:
        raise ValueError('selection exceeds bounded viewer limits')
    vq_reports=[]
    if texture_format=='vq':
        from vq_texture import encode
        packed=[]
        for width,height,pixels in textures:
            pixels,quality=encode(pixels,width,height); packed.append((width,height,pixels)); vq_reports.append(quality)
        textures=packed
    texture_bytes=sum(len(t[2]) for t in textures)
    if texture_bytes>MAX_TEXTURE_BYTES: raise ValueError('texture budget exceeded')
    low=[min(v[k] for v in vertices) for k in range(3)]
    high=[max(v[k] for v in vertices) for k in range(3)]
    center=[(a+b)/2 for a,b in zip(low,high)]
    radius=max(math.sqrt(sum((v[k]-center[k])**2 for k in range(3))) for v in vertices)
    if not math.isfinite(radius) or radius<1e-12: raise ValueError('degenerate scene')
    data=bytearray(struct.pack('<4s7I',b'DCT1',1,len(vertices),len(indices),len(batches),len(textures),texture_bytes,0))
    for vertex in vertices:
        data+=struct.pack('<5fI',*((vertex[k]-center[k])/radius for k in range(3)),*vertex[3:])
    data+=struct.pack('<'+'H'*len(indices),*indices)
    for batch in batches: data+=struct.pack('<4I',*batch)
    for width,height,pixels in textures:
        data+=struct.pack('<HHII',width,height,2 if texture_format=='vq' else 1,len(pixels))+pixels
    return bytes(data),{'triangles':len(indices)//3,'vertices':len(vertices),'batches':len(batches),
                        'textures':len(textures),'texture_bytes':texture_bytes,'bytes':len(data),
                        'geometry_bytes':len(vertices)*24+len(indices)*2,'mesh_index':mesh_index,
                        'original_center':center,'original_radius':radius,'truncated':False,
                        'texture_format':texture_format,'vq_quality':vq_reports}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--mesh-index',type=int,required=True)
    parser.add_argument('--texture-size',type=int,default=128)
    parser.add_argument('--texture-format',choices=('argb1555','vq'),default='argb1555')
    args=parser.parse_args()
    try:
        doc,binary=read_glb(args.input)
        data,report=convert(doc,binary,args.mesh_index,args.texture_size,args.texture_format)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_bytes(data)
        print(json.dumps(report,indent=2))
    except (ValueError,KeyError,IndexError,TypeError,OSError,struct.error) as error:
        parser.error(str(error))

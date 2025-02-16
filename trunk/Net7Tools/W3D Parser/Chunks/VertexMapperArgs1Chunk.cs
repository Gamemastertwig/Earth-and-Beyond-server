﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO;
using System.ComponentModel;

namespace WestWood3D.Chunks
{
    class VertexMapperArgs1Chunk : Chunk
    {
        private String mapper_args;
        public VertexMapperArgs1Chunk()
        {
        }

        public override void parse(BinaryReader br, ChunkMap chkMap, Boolean dbg, int chunkLength)
        {
            if (dbg) Console.Out.WriteLine("|----| " + ChunkHeader.W3D_CHUNK_VERTEX_MAPPER_ARGS1);

            HeaderID = (int)ChunkHeader.W3D_CHUNK_VERTEX_MAPPER_ARGS1;
            HeaderName = ChunkHeader.W3D_CHUNK_VERTEX_MAPPER_ARGS1.ToString();

            mapper_args = br.ReadChars(chunkLength).ToString();

            if (dbg)
            {
                Console.Out.WriteLine("\t\t Stage1 Mapper Args: " + mapper_args);
            }
        }

        [CategoryAttribute("Chunk Values"), ReadOnlyAttribute(true), DescriptionAttribute("A string argument specifying vertex options.")]
        public String MapperArgs
        {
            get { return mapper_args; }
            set { mapper_args = value; }
        }
    }
}

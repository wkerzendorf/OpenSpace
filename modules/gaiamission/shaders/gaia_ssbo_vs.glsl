/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2018                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#version __CONTEXT__

#include "floatoperations.glsl"

// Keep in sync with renderoption.h:RenderOption enum
const int RENDEROPTION_STATIC = 0;
const int RENDEROPTION_COLOR = 1;
const int RENDEROPTION_MOTION = 2; 
const float EPS = 1e-5;
const float Parsec = 3.0856776e16;

layout (std430) buffer ssbo_idx_data { 
  int starsPerChunk[];
};

layout (std430) buffer ssbo_comb_data { 
  float allData[];
};

in int gl_VertexID;

out vec2 vs_brightness;
out vec4 vs_gPosition;
out float vs_starDistFromSun;
out float vs_cameraDistFromSun;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time; 
uniform int renderOption;

uniform int maxStarsPerNode;
uniform int valuesPerStar;
uniform int nChunksToRender;

// Use binary search to find the chunk containing our star ID.
int findChunkId(int left, int right, int id) {
    
    while ( left <= right ) {
        int middle = (left + right) / 2;
        int firstStarInChunk = starsPerChunk[middle];
        if (left == right || (firstStarInChunk <= id && id < starsPerChunk[middle+1])) {
            return middle;
        }
        else if (id < firstStarInChunk) {
            // Go smaller
            right = middle - 1;
        }
        else {
            // Go bigger
            left = middle + 1;
        }
    }
    return -1;
}

void main() {
    // Fetch our data.
    int chunkId = findChunkId(0, nChunksToRender - 1, gl_VertexID);
    // Fail safe - this should never happen!
    if (chunkId == -1) {
        vs_gPosition = vec4(0.0);    
        gl_Position = vec4(0.0);
        return;
    }
    int placeInChunk = gl_VertexID - starsPerChunk[chunkId];
    int firstStarInChunk = valuesPerStar * maxStarsPerNode * chunkId; // Chunk offset
    int nStarsInChunk = starsPerChunk[chunkId + 1] - starsPerChunk[chunkId]; // Stars in current chunk.
    // Remove possible duplicates.
    if (nStarsInChunk <= 0) {
        vs_gPosition = vec4(0.0);    
        gl_Position = vec4(0.0);
        return;
    }
    
    int startOfPos = firstStarInChunk + placeInChunk * 3;
    vec3 in_position = vec3(allData[startOfPos], allData[startOfPos + 1], allData[startOfPos + 2]);
    vec2 in_brightness = vec2(0.0);
    vec3 in_velocity = vec3(0.0);

    if ( renderOption != RENDEROPTION_STATIC ) {
        int startOfCol = firstStarInChunk + nStarsInChunk * 3 + placeInChunk * 2;
        in_brightness = vec2(allData[startOfCol], allData[startOfCol + 1]);

        if ( renderOption == RENDEROPTION_MOTION ) {
            int startOfVel = firstStarInChunk + nStarsInChunk * 5 + placeInChunk * 3;
            in_velocity = vec3(allData[startOfVel], allData[startOfVel + 1], allData[startOfVel + 2]);
        } 
    }
    vs_brightness = in_brightness;

    // Convert kiloParsec to meter.
    vec4 modelPosition = vec4(in_position * 1000 * Parsec, 1.0);

    // Add velocity if we've read any.
    modelPosition.xyz += time * in_velocity;

    // Apply camera transforms.
    vec4 viewPosition = view * model * modelPosition;
    vec4 sunPosition = view * model * vec4(0.0f, 0.0f, 0.0f, 1.0f);

    vs_starDistFromSun = safeLength(modelPosition);
    vs_cameraDistFromSun = safeLength(sunPosition);

    // Remove stars without position, happens when VBO chunk is stuffed with zeros.
    // Has to be done in Geometry shader because Vertices cannot be discarded here.
    if ( length(in_position) > EPS ){
        vs_gPosition = viewPosition;    
        gl_Position = projection * viewPosition;
    } else {
        vs_gPosition = vec4(0.0);    
        gl_Position = vec4(0.0);
    }
}
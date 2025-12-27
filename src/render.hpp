#pragma once

#include "types.hpp"
#include "world.hpp"

#include <map>
#include <array>
#include <string>
#include <vector>

extern const int CHUNK_SIZE;
extern int CHUNK_X_COUNT;
extern int CHUNK_Y_COUNT;
extern int CHUNK_Z_COUNT;
extern std::vector<ChunkMesh> chunkMeshes;
extern GLuint gAtlasTex;
extern const int ATLAS_COLS;
extern const int ATLAS_ROWS;
extern const int ATLAS_TILE_SIZE;
extern std::map<BlockType, int> gBlockTile;
extern int gAndTopTile;
extern int gOrTopTile;
extern int gNotTopTile;
extern int gXorTopTile;
extern const std::map<char, std::array<uint8_t, 5>> FONT5x4;
extern const int MAX_STACK;
extern const int INV_COLS;
extern const int INV_ROWS;

int chunkIndex(int cx, int cy, int cz);
void markAllChunksDirty();
void markChunkFromBlock(int x, int y, int z);
void markNeighborsDirty(int x, int y, int z);
void ensureVbo(GLuint &vbo);
int tileIndexFor(BlockType b);
void createAtlasTexture();
GLuint loadTextureFromBMP(const std::string &path);
GLuint loadCubemapFromBMP(const std::array<std::string, 6> &paths);
void buildChunkMesh(const World &world, int cx, int cy, int cz);
void drawNpcBlocky(const NPC &npc);
void drawSkybox(GLuint cubemap, float size);

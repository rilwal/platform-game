
//Copyright (c) 2015 Jason Light
//License: MIT

#include "Level.h"

#include <vector>

#include "Types.h"
#include "Maths.h"
#include "Utility.h"
#include "MemoryManager.h"
#include "LogManager.h"

Level::Level() {
	Chunks = (Chunk**)MemoryManager::AllocateMemory(sizeof(Chunk*) * UINT16_MAX);
	//Sprites = (Sprite**)MemoryManager::AllocateMemory(sizeof(_Sprite*) * UINT8_MAX);
}

Level::~Level() {
	VirtualFree((void*)Chunks, sizeof(Chunk) * UINT16_MAX, 8);
}

//Essentially a hash funciton for our hash table
u32 Level::LookupLocation(u16 X, u16 Y) {
	//TODO: Make this less bad ;-)
	//We need a hash function which minimizes collisisons

	return (Y * UINT16_MAX + X) % UINT16_MAX;
}

Chunk* Level::SetChunk(u16 X, u16 Y, Chunk* C) {

	//Create and initialize a new chunk
	Chunk* NewChunk = new Chunk();
	memcpy((void*)NewChunk, (void*)C, sizeof(*C));

	//NewChunk->Entities = {};

	NewChunk->X = X;
	NewChunk->Y = Y;

	//Handle hashmap collisions

	Chunk** ch = &Chunks[LookupLocation(X, Y)];

	if (*ch != NULL) {
		Chunk* par = NULL;

		while (*ch != NULL) {
			if (&(*ch)->Collission != NULL) {
				par = *ch;
			}

			ch = &(*ch)->Collission;
		}

		par->Collission = NewChunk;
	}

	*ch = NewChunk;

	ExistingChunks.push_back({ X, Y });

	SetChunkGeometry(*ch);

	return *ch;
}

Chunk* Level::GetChunk(u16 X, u16 Y){
	Chunk* Location = Chunks[LookupLocation(X, Y)];
	
	if (Location == nullptr) {
		Chunk* C = MemoryManager::AllocateMemory<Chunk>(1);
		return SetChunk(X, Y, C);
	}
	while (Location->X != X || Location->Y != Y) {
		if (Location->Collission == nullptr) {
  			Chunk* C = MemoryManager::AllocateMemory<Chunk>(1);
			return SetChunk(X, Y, C);
		}

		Location = Location->Collission;
	}
	return Location;
}

#define ChunkLoc(x,y) ((y)*16+(x))

std::vector<iRect> Level::GenerateCollisionGeometryFromChunk(u16 X, u16 Y) {
	Chunk* C = GetChunk(X, Y);
	return GenerateCollisionGeometryFromChunk(C);
}

std::vector<iRect> Level::GenerateCollisionGeometryFromChunk(Chunk* C) {
	std::vector<iRect> CollisionGeometry;

	if (C == nullptr) {
		return{};
	}

	u16 X = C->X;
	u16 Y = C->Y;

	bool Visited[16 * 16] = {};

	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 16; x++) {
			if (C->Grid[ChunkLoc(x, y)].Collision &! Visited[ChunkLoc(x, y)]) {
				int Top = (y + Y * 16) * 32;
				int Left = (x + X * 16) * 32;
				int Width = 32;
				int Height = 32;

				int n = 1;
				while (C->Grid[ChunkLoc(x + n, y)].Collision &! Visited[ChunkLoc(x + n, y)]) {
					if (x + n >= 16) break;
					Visited[ChunkLoc(x + n, y)] = true;
					n++;
					Width += 32;
				}

				int m = 1;
				while (C->Grid[ChunkLoc(x, y + m)].Collision &! Visited[ChunkLoc(x, y + m)]) {
					if (y + m >= 16) break;

					bool wide = true;
					for (int i = 0; i < Width / 32; i++) {
						if (!C->Grid[ChunkLoc(x + i, y + m)].Collision) {
							wide = false;
							break;
						}
					}

					if (!wide) break;

					for (int i = 0; i < Width / 32; i++)
						Visited[ChunkLoc(x + i, y + m)] = true;
					m++;
					Height += 32;
				}

				CollisionGeometry.push_back({ Left, Top, Width, Height });
			}
		}
	}

	return CollisionGeometry;
}

void Level::SetChunkGeometry(Chunk* C) {
	if (C->Geometry) delete C->Geometry;
	C->Geometry = new std::vector<iRect>;
	*C->Geometry = GenerateCollisionGeometryFromChunk(C);
}

void Level::LoadFromAsset(Asset asset) {
	byte* data = (byte*)asset.Memory;
	u32 fp = 0;

	if (*(u16*)&data[0] != 0x4C41) {
		GlobalLog.Write("Magic number doesn't match, aborting");
		return;
	}

	fp += 4;

	int NameLength = strlen((const char *)&data[fp]);
	fp += NameLength + 1;

	int AuthorLength = strlen((const char *)&data[fp]);
	fp += AuthorLength + 1;

	u32 NumberOfAssetFiles = *(u32*)&data[fp];
	fp += 4;

	Sprites.push_back({});

	for (int i = 0; i < NumberOfAssetFiles; i++) {
		char * AssetFileName = (char *)(data + fp);
		char str[256];
		sprintf_s(str, "Loading assets from '%s'", AssetFileName);
		GlobalLog.Write(str);

		fp += strlen(AssetFileName) + 1;

		u32 NumberOfAssets = *(u32*)&data[fp];

		fp += 4;
		
		AssetFile CurrentAssetFile(AssetFileName);

		for (int j = 0; j < NumberOfAssets; j++) {
			Sprite spr;
			spr.Load(CurrentAssetFile, *(u32*)&data[fp]);
			fp += 4;
			Sprites.push_back(spr);
		}
	}

	u32 ChunksToLoad = *(u32*)&data[fp];
	fp += 4;

	Chunk* Chunks = MemoryManager::AllocateMemory<Chunk>(ChunksToLoad);

	for (int i = 0; i < ChunksToLoad; i++) {
		u16 X = *(u16*)&data[fp];
		fp += 2;

		u16 Y = *(u16*)&data[fp];
		fp += 2;

		Chunk Chunk = Chunks[i];

		//Chunks[i].Entities = *(new DoubleLinkedList<GameObject>());

		for (int j = 0; j < 256; j++) {
			Chunk.Grid[j] = { *(u16*)&data[fp], *(u8*)&data[fp + 2] };
			fp += 3;
		}

		SetChunk(X, Y, &Chunk);
		
	}
}

void Level::Update(double DeltaTime, std::vector<IVec2>& Chunks) {

	std::vector<iRect> Geometry;

	for (auto C : Chunks) {
		auto Chunk = GetChunk(C.X, C.Y);

		if (Chunk == nullptr) continue;

		//std::vector<iRect> ChunkGeometry = GenerateCollisionGeometryFromChunk(Chunk);
		Geometry.insert(Geometry.end(), Chunk->Geometry->begin(), Chunk->Geometry->end());
	}

	auto Entity = Entities.First;

	while (Entity != nullptr) {
		Entity->Item->Update(DeltaTime, Geometry);
		Entity = Entity->Next;
	}
}

void Level::UpdateChunk(u16 X, u16 Y, double DeltaTime, std::vector<iRect>& Geometry) {
	Chunk* C = GetChunk(X, Y);

	if (C == nullptr) {
		return;
	}

	//auto Entity = C->Entities.First;

	//while (Entity != nullptr) {
	//	Entity->Item->Update(DeltaTime, Geometry);
	//	Entity = Entity->Next;
	//}
}

void Level::SpawnEntity(GameObject * Object, u32 X, u32 Y) {
	Object->Position = { (float)X, (float)Y };
	Entities.Insert(Object);
	
	/*int ChunkX = X / 16;
	int ChunkY = Y / 16;

	Chunk* C = GetChunk(ChunkX, ChunkY);

	if (C == nullptr) {
		return;
	}
	*/
	//C->Entities.Insert(Object);
}

void Level::DrawChunk(Renderer* Renderer, u16 X, u16 Y) {
	Chunk* C = GetChunk(X, Y);

	if (C == nullptr) {
		return;
	}

	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			if (C->Grid[y * 16 + x].Texture != NULL) {
				Renderer->DrawSprite(&Sprites[C->Grid[y * 16 + x].Texture], 32 * (16 * X + x), 32 * (16 * Y + y));
			}
		}
	}
}
void Level::DrawChunkCollisionGeometry(Renderer * Renderer, u16 X, u16 Y){
	Chunk* C = GetChunk(X, Y);

	if (C == nullptr) {
		return;
	}

	for (auto R : *C->Geometry) {
		Renderer->DrawRectangleBlendWS(R.X + 2, R.Y + 2, R.W - 4, R.H - 4, rgba(255, 0, 255, 128));
	}
}

/*
void Level::DrawChunkEntities(Renderer * Renderer, u16 X, u16 Y){
	Chunk* C = GetChunk(X, Y);

	if (C == nullptr) {
		return;
	}

	//auto Entity = C->Entities.First;

	while (Entity != nullptr) {
		Entity->Item->Draw(Renderer);
		Entity = Entity->Next;
	}
}
*/
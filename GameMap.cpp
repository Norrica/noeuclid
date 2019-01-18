#include "GameMap.h"
#include <cstring>
#include <functional>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>

using namespace std;

void GameMap::die() {
    rooms[curroom].reset();
    GameAttempt++;
}


void GameMap::collision(CollisionProbe * ddat) {
    ///XXX TODO: pointers for where we're aimed should come in here.
    //printf( "CC %f %f %f  (%f)\n", ddat->TargetLocation.r, ddat->TargetLocation.g, ddat->TargetLocation.b, ddat->Normal.a ); 
}

/**
 * read into string until a line containing  only '}'
 * Used to parse initialization and run scripts from the level file
 * TODO: Change syntax or use a real parser
 */
string readScript(ifstream& file, int& lineNum) {
    string script = "", line;
    int beginning = lineNum;
    while(getline(file, line)) {
        if(line=="}") return script;
        script += line+"\n";
        lineNum++;
    }
    cout<<"Error: script beginning at line "<<beginning<<" not terminated."<<endl;
    return "";
}

/** Loads a list of rooms (block positions and scripts) */
void GameMap::loadRooms(string fname) {
    rooms.clear();
    ifstream file(fname);
    printf("Loading %s\n", fname.c_str());
    if(!file.is_open()) printf("Could not open rooms.txt");
    string line;
    int rid = 0;
    int lineNum = 0;
    while(getline(file, line)) {
        lineNum++;
        if(line.back()=='\r') line.pop_back();
        if(line.length()==0) continue;
        
        if(line[0]=='#') continue;
        if(line == "RunScript {") {
            if(rooms[rid].runscript) cout <<lineNum<<": Error: Already has run script." << endl;
            rooms[rid].runscript = tcc.eval<void(double, double)>("void fun(double timeInRoom, double worldDeltaTime) {"+readScript(file, lineNum)+"}");
            continue;
        }
        if(line == "InitScript {") {
            if(rooms[rid].initscript) cout <<lineNum<<": Error: Already has init script." << endl;
            rooms[rid].initscript = tcc.eval<void(void)>("void fun() {"+readScript(file, lineNum)+"}");
            continue;
        }
        for(char c: "()") line.erase(remove(line.begin(),line.end(),c),line.end()); // remove parens
        istringstream l(line);
        string cmd; l >> cmd;
        if(cmd == "StartAtRoom") l>>startroom;
        else if(cmd == "Room") {
            l >> rid;
            if(rid>=int(rooms.size())) rooms.resize(rid+1);
        }
        else if(cmd == "Start") l >> rooms[rid].start;
        else if(cmd == "Exit") l >> rooms[rid].exitr1 >> rooms[rid].exitr2;
        else if(cmd == "Time") l >> rooms[rid].maxTime;
        else if(initfuncs.count(cmd))
            rooms[rid].inits.push_back(initfuncs[cmd](l));
        else if(runfuncs.count(cmd))
            rooms[rid].runs.push_back(runfuncs[cmd](l));
        else cout <<lineNum<<": Error: Invalid command " << cmd << endl;
        if(l.fail()) cout<<lineNum<<": Error: line formatting error "<<endl;
    }
}

void GameMap::update() {
    if (gOverallUpdateNo % 30 == 0 && fileChanged("rooms.txt")) {
        loadRooms("rooms.txt");
    }
    
    if (gOverallUpdateNo == 0) {
        curroom = startroom;
        gPosition = rooms[curroom].start;
    }

    if (curroom != lastroom) {
        curroom = (curroom%rooms.size()+rooms.size())%rooms.size();
        printf("Switching to room %d\n", curroom);
        GameTimer = rooms[curroom].maxTime;
        rooms[curroom].begin();
        lastroom = curroom;
    }

    gDaytime += worldDeltaTime;
    GameTimer -= worldDeltaTime;
    if (GameTimer < 0) {
        die();
    }

    rooms[curroom].update();

    sprintf(gDialog, "Room: %d\n", curroom);

    

    UpdatePickableBlocks(); //Draw the pickables.

    //printf( "%f %f %f   %f %f %f\n", targetx, targety, targetz, gDirectionX, gDirectionY, gDirectionZ );
    if (gMouseLastClickButton != -1) {
        printf("Click on: %f %f %f [%d]\n", gTargetHit.x, gTargetHit.y, gTargetHit.z, gMouseLastClickButton );
        if (gMouseLastClickButton == 0) {
            //Left-mouse
            Vec3f target = gTargetHit + gTargetNormal * .5;
            Vec3f dist = (target-gPosition);
            
            PickableClick(true, target, dist.len());
        } else if (gMouseLastClickButton == 2 || gMouseLastClickButton == 1 ) {
            //Right-mouse
            Vec3f target = gTargetHit - gTargetNormal * .5;
            Vec3f dist = (target-gPosition);

            PickableClick(false, target, dist.len());
        }
        gMouseLastClickButton = -1;
    }

    //We're on the ground.  Make sure we don't die.
    if (gTimeSinceOnGround < .001) {
        Vec3i p = {(int)gPosition.x, (int)gPosition.y, (int)gPosition.z};
        //printf( "%d %d %d\n", px, py, pz );
        if (IsOnDeathBlock(p)) {
            printf("DIE\n");
            die();
        }
    }

    if (PlayerInRangeV({2, 40, 63}, {16, 16, 4})) {
        int lx = ((int) gPosition.x) - 2;
        int ly = ((int) gPosition.y) - 40;
        sprintf(gDialog, "OnTile %d\n", lx + ly * 16);
    }
}

void GameMap::setRoom(int i, bool reset) {
    int s = rooms.size();
    curroom = (i%s+s)%s;
    if(reset) rooms[curroom].reset();
}

void GameMap::AddDeathBlock(Vec3i p) {
    //Don't add multiple.
    for (DeathBlock& db:DeathBlocks)
        if (db.p == p) return;

    for (DeathBlock& db:DeathBlocks) {
        if (db.in_use == 0) {
            db.in_use = 1;
            db.p = p;
            break;
        }
    }
}
bool GameMap::IsOnDeathBlock(Vec3i p) {
    for (DeathBlock& db:DeathBlocks) {
        if (db.p.x == p.x && db.p.y == p.y && (db.p.z == p.z || db.p.z == p.z - 1))
            return 1;
    }
    return 0;
}

//Returns id if pickable is there.
//Returns -1 if no block.
//Returns -2 if block tween incomplete.
int GameMap::GetPickableAt(Vec3i p) {
    int i;
    for (i = 0; i < MAX_PICKABLES; i++) {
        struct PickableBlock * pb = &PBlocks[i];
        if(pb->in_use) printf("%d %d %d\n",pb->p.x,pb->p.y,pb->p.z);
        if (pb->in_use && pb->p == p) {
            return (pb->density > .99) ? i : -2;
        }
    }
    return -1;
}

void GameMap::DissolvePickable(int pid) {
    PBlocks[pid].phasing = -1;
}

//If -1, no pickables left.
//If -2, Pickable already there.
//Otherwise returns which pickable!
//initial_density should be 0 unless you want to shorten (+) or lengthen (-) tween.
int GameMap::PlacePickableAt(Vec3i p, float initial_density) {
    int i;

    for (i = 0; i < MAX_PICKABLES; i++) {
        struct PickableBlock * pb = &PBlocks[i];
        if (!pb->in_use) {
            break;
        }
    }
    if (i == MAX_PICKABLES)
        return -2;

    struct PickableBlock * pb = &PBlocks[i];
    pb->p = p;
    pb->phasing = 1;
    pb->in_use = 1;
    pb->density = initial_density;
    return 0;
}

void GameMap::ClearPickableBlocks() {
    for (PickableBlock& pb : PBlocks) {
        pb.p = Vec3i{0,0,0};
        pb.phasing = 0;
        pb.density = 0;
        pb.in_use = 0;
    }
    pickables_in_inventory = 0;
}

//Redraw 

void GameMap::UpdatePickableBlocks() {
    for (PickableBlock& pb : PBlocks) {
        if (!pb.in_use) continue;

        pb.density += worldDeltaTime * pb.phasing;

        if (pb.density >= 1.0 && pb.phasing > 0) {
            pb.density = 1.0;
            pb.phasing = 0;
        }
        if (pb.density <= 0 && pb.phasing < 0) {
            pb.density = 0.0;
            pb.phasing = 0.0;
            pb.in_use = 0;
        }
        float drawden = (pb.density < 0) ? 0.0 : ((pb.density > 1.0) ? 1.0 : pb.density);

        if (drawden > .01) {
            PaintRangeV(pb.p, {1, 1, 1}, {1,190,byte(drawden * 200), PICKABLE_CELL});
        } else {
            ClearCellV(pb.p);
        }
    }
}

void GameMap::PickableClick(bool left, Vec3f p, float dist) {
    //TODO calculate dist some other way (through portals)
    //if (dist > 4) return;
    if (left) {
        if (pickables_in_inventory > 0) {
            PlacePickableAt({(int)p.x,(int)p.y,(int)p.z}, 0.0);
            pickables_in_inventory--;
        } else {
            printf("No blocks.\n");
        }
    } else {
        int r = GetPickableAt({(int)p.x,(int)p.y,(int)p.z});

        if (r >= 0) {
            pickables_in_inventory++;
            PBlocks[r].phasing = -1;
        } else {
            printf("No pickable at (%d,%d,%d)",(int)p.x,(int)p.y,(int)p.z);
        }
    }
}


/*
    =====================================================================
    ORBITAL - Satellite Health Monitoring & Collision Avoidance (mini project)
    =====================================================================

    Mini project made using Data Structures + OOP concepts:
        1) AVL Tree        -> stores debris grouped by "sector" (spatial index)
        2) Min Heap        -> finds the nearest collision threat first
        3) Circular Queue  -> a template class, stores last 10 battery readings
        4) OOP             -> abstract base class Satellite + 2 derived classes
        5) File Handling   -> writes all events to blackbox.txt

    Now with:
        - A simple Solar System: 8 planets at fixed positions. The user
          picks which planet each satellite orbits, and the status line
          always shows it (e.g. "orbiting Earth").
        - Everything is entered by the user at the start: how many
          satellites, each one's name/type/planet, how many ticks to run,
          how long a repair takes, and how much starting debris to seed.
        - Satellites that break down now actually COME BACK ONLINE: when
          a satellite is sent for repair you're told exactly how many
          ticks until it's back (and the sim time it'll happen at), the
          countdown shows on every status update, and a clear
          "REPAIR COMPLETE - back ONLINE!" message prints the moment it
          returns to service.

    How it works (short version):
        - Satellites move in a straight line (simple physics, no real
          gravity - just position = position + velocity * time), in a
          rough circular orbit around whichever planet you assigned them.
        - Some space debris also moves around them.
        - Every "tick" (1 simulated second):
              1. A random hazard may occur  (Chaos Engine)
              2. Debris moves
              3. All debris positions are inserted fresh into an AVL tree
                 (grouped by sector, so we don't have to check EVERY debris
                 object against every satellite - just nearby sectors)
              4. Satellites move + run their own diagnostics
                 (a disabled satellite instead ticks down its repair timer)
              5. Nearby debris are pushed into a Min-Heap, so the closest
                 danger always comes out first
              6. If a satellite gets too close to a debris object, it does
                 an evasive burn (changes its velocity a little)
              7. Status is printed on screen and saved to blackbox.txt

    Compile:
        g++ orbital.cpp -o orbital
    Run:
        ./orbital
    (it will ask you some setup questions first)
    =====================================================================
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <unistd.h>   // for usleep() -> only works on Linux/Mac, not Windows

using namespace std;


/* =====================================================================
   1) VECTOR3 -> simple struct to hold x, y, z (used for position/velocity)
   ===================================================================== */
struct Vector3 {
    double x, y, z;

    Vector3() { x = 0; y = 0; z = 0; }
    Vector3(double a, double b, double c) { x = a; y = b; z = c; }

    // operator overloading (we learnt this in OOP class)
    Vector3 operator+(const Vector3 &o) const {
        return Vector3(x + o.x, y + o.y, z + o.z);
    }
    Vector3 operator-(const Vector3 &o) const {
        return Vector3(x - o.x, y - o.y, z - o.z);
    }
    Vector3 operator*(double s) const {
        return Vector3(x * s, y * s, z * s);
    }
};

// returns straight-line distance between two points
double dist3D(Vector3 a, Vector3 b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}


/* =====================================================================
   2) CIRCULAR QUEUE -> template class (works for any data type T)
      Used to store the last few battery readings of a satellite without
      the array growing forever - old values just get overwritten.
   ===================================================================== */
template <class T>
class CircularQueue {
    vector<T> data;
    int maxSize;
    int start;   // index of the oldest element

public:
    CircularQueue(int size = 10) {
        maxSize = size;
        start = 0;
    }

    void push(T value) {
        if ((int)data.size() < maxSize) {
            data.push_back(value);          // still filling up
        } else {
            data[start] = value;            // overwrite the oldest value
            start = (start + 1) % maxSize;
        }
    }

    vector<T> getAll() {
        vector<T> result;
        for (int i = 0; i < (int)data.size(); i++) {
            int idx = (start + i) % data.size();
            result.push_back(data[idx]);
        }
        return result;
    }
};


/* =====================================================================
   3) MIN HEAP -> used for the "which debris is closest" problem.
      Every entry is a Threat (satellite id, debris id, distance).
      The heap always keeps the smallest distance at the top (index 0).
   ===================================================================== */
struct Threat {
    int satId;
    int debId;
    double distance;
};

class MinHeap {
    vector<Threat> h;

    void swapThreat(int i, int j) {
        Threat temp = h[i];
        h[i] = h[j];
        h[j] = temp;
    }

public:
    bool isEmpty() {
        return h.empty();
    }

    void push(Threat t) {
        h.push_back(t);
        int i = h.size() - 1;
        // bubble the new element up until heap property is satisfied
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (h[parent].distance > h[i].distance) {
                swapThreat(parent, i);
                i = parent;
            } else {
                break;
            }
        }
    }

    Threat popMin() {
        Threat top = h[0];
        h[0] = h[h.size() - 1];
        h.pop_back();

        // bubble the root down until heap property is satisfied
        int i = 0;
        int n = h.size();
        while (true) {
            int left = 2 * i + 1;
            int right = 2 * i + 2;
            int smallest = i;
            if (left < n && h[left].distance < h[smallest].distance) smallest = left;
            if (right < n && h[right].distance < h[smallest].distance) smallest = right;
            if (smallest == i) break;
            swapThreat(i, smallest);
            i = smallest;
        }
        return top;
    }
};


/* =====================================================================
   4) AVL TREE -> stores debris IDs grouped by "sector key" so that when
      a satellite wants to know "what debris is near me", we only search
      nearby sectors instead of looping through every debris object.

      NOTE: We never delete from this tree - every tick we just build a
      brand new tree from scratch (clear + insert again). That means we
      don't need to write AVL deletion, which keeps the code much simpler.
   ===================================================================== */
struct AVLNode {
    long long key;
    vector<int> debrisIds;   // more than one debris object can share a sector
    AVLNode *left, *right;
    int height;
};

// turns a 3D position into one sector key (divides space into 100-unit cubes)
long long getSectorKey(Vector3 p) {
    long long bx = (long long) floor(p.x / 100.0);
    long long by = (long long) floor(p.y / 100.0);
    long long bz = (long long) floor(p.z / 100.0);
    // combine the 3 numbers into a single unique key
    return bx * 1000000LL + by * 1000LL + bz;
}

class AVLTree {
    AVLNode *root;

    int getHeight(AVLNode *n) { return n ? n->height : 0; }
    int getBalance(AVLNode *n) { return n ? getHeight(n->left) - getHeight(n->right) : 0; }
    int myMax(int a, int b) { return (a > b) ? a : b; }

    AVLNode* rotateRight(AVLNode *y) {
        AVLNode *x = y->left;
        AVLNode *t2 = x->right;
        x->right = y;
        y->left = t2;
        y->height = 1 + myMax(getHeight(y->left), getHeight(y->right));
        x->height = 1 + myMax(getHeight(x->left), getHeight(x->right));
        return x;
    }

    AVLNode* rotateLeft(AVLNode *x) {
        AVLNode *y = x->right;
        AVLNode *t2 = y->left;
        y->left = x;
        x->right = t2;
        x->height = 1 + myMax(getHeight(x->left), getHeight(x->right));
        y->height = 1 + myMax(getHeight(y->left), getHeight(y->right));
        return y;
    }

    AVLNode* insertHelper(AVLNode *node, long long key, int id) {
        if (node == NULL) {
            AVLNode *n = new AVLNode();
            n->key = key;
            n->debrisIds.push_back(id);
            n->left = NULL;
            n->right = NULL;
            n->height = 1;
            return n;
        }

        if (key < node->key) {
            node->left = insertHelper(node->left, key, id);
        } else if (key > node->key) {
            node->right = insertHelper(node->right, key, id);
        } else {
            node->debrisIds.push_back(id);   // same sector, just add to the list
            return node;
        }

        node->height = 1 + myMax(getHeight(node->left), getHeight(node->right));
        int balance = getBalance(node);

        // 4 standard AVL rotation cases
        if (balance > 1 && key < node->left->key)
            return rotateRight(node);
        if (balance < -1 && key > node->right->key)
            return rotateLeft(node);
        if (balance > 1 && key > node->left->key) {
            node->left = rotateLeft(node->left);
            return rotateRight(node);
        }
        if (balance < -1 && key < node->right->key) {
            node->right = rotateRight(node->right);
            return rotateLeft(node);
        }
        return node;
    }

    vector<int> searchHelper(AVLNode *node, long long key) {
        if (node == NULL) return vector<int>();
        if (key == node->key) return node->debrisIds;
        if (key < node->key) return searchHelper(node->left, key);
        return searchHelper(node->right, key);
    }

    int countHelper(AVLNode *node) {
        if (node == NULL) return 0;
        return 1 + countHelper(node->left) + countHelper(node->right);
    }

    void clearHelper(AVLNode *node) {
        if (node == NULL) return;
        clearHelper(node->left);
        clearHelper(node->right);
        delete node;
    }

public:
    AVLTree() { root = NULL; }
    ~AVLTree() { clearHelper(root); }

    void insert(long long key, int id) { root = insertHelper(root, key, id); }
    vector<int> search(long long key) { return searchHelper(root, key); }
    int countNodes() { return countHelper(root); }
};


/* =====================================================================
   5) DEBRIS -> a piece of space junk. It just moves at constant velocity.
   ===================================================================== */
struct Debris {
    int id;
    Vector3 pos;
    Vector3 vel;
};

// simple linear search - debris count is small so this is fine
Debris* findDebrisById(vector<Debris> &list, int id) {
    for (int i = 0; i < (int)list.size(); i++) {
        if (list[i].id == id) return &list[i];
    }
    return NULL;
}


/* =====================================================================
   6) SOLAR SYSTEM -> a Planet is just a name + a fixed position. Every
      satellite orbits around ONE planet's position instead of empty
      space, and remembers that planet's name so we can show it later.

      NOTE: real planets orbit the Sun too, but that's a whole extra
      simulation by itself - for this mini project the planets just sit
      at fixed spots (roughly ordered by real distance from the Sun,
      spread around in a circle so they don't overlap).
   ===================================================================== */
struct Planet {
    string name;
    Vector3 position;
};

vector<Planet> buildSolarSystem() {
    vector<Planet> planets;

    string names[] = {"Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};
    double distances[] = {2000, 3000, 4200, 5500, 9000, 12000, 15000, 18000};
    int count = 8;

    for (int i = 0; i < count; i++) {
        double angleDeg = i * (360.0 / count);         // spread planets evenly around the Sun
        double rad = angleDeg * 3.14159265 / 180.0;

        Planet p;
        p.name = names[i];
        p.position = Vector3(distances[i] * cos(rad), distances[i] * sin(rad), 0);
        planets.push_back(p);
    }
    return planets;
}


/* =====================================================================
   7) SATELLITE (OOP part) -> abstract base class + 2 derived classes
   ===================================================================== */

// how many ticks a broken satellite takes to come back online once a
// repair crew is dispatched - the user sets this at the start in main()
int REPAIR_DURATION_TICKS = 20;

class Satellite {
protected:
    int id;
    string name;
    string planetName;    // which planet this satellite orbits (just for display)
    Vector3 pos, vel;
    double battery;      // percent, 0-100
    double software;     // percent, 0-100
    double fuel;          // kg of propellant left
    bool thrusterOk;
    bool disabled;         // true once it needs a physical repair crew
    int repairTicksRemaining;   // counts down to 0 while disabled, then it comes back online

    CircularQueue<double> batteryHistory;   // template class in use here

public:
    Satellite(int id_, string name_, Vector3 pos_, Vector3 vel_) {
        id = id_;
        name = name_;
        planetName = "Unknown";
        pos = pos_;
        vel = vel_;
        battery = 100;
        software = 100;
        fuel = 50;
        thrusterOk = true;
        disabled = false;
        repairTicksRemaining = 0;
    }

    virtual ~Satellite() {}

    // ---- pure virtual functions: every satellite TYPE must define these ----
    virtual string getType() = 0;
    virtual void runDiagnostics(ofstream &logFile, double simTime) = 0;
    virtual string getStatusLine() = 0;

    // ---- shared behaviour for all satellites ----
    void moveOneStep(double dt) {
        if (disabled) return;
        pos = pos + vel * dt;
        battery -= 0.02 * dt;
        if (battery < 0) battery = 0;
        batteryHistory.push(battery);
    }

    void burn(Vector3 dv) { vel = vel + dv; }

    bool useFuel(double amount) {
        if (fuel < amount) return false;
        fuel -= amount;
        return true;
    }

    double getAvgBattery() {
        vector<double> vals = batteryHistory.getAll();
        if (vals.empty()) return battery;
        double sum = 0;
        for (int i = 0; i < (int)vals.size(); i++) sum += vals[i];
        return sum / vals.size();
    }

    // ---- these get called by the Chaos Engine to cause random damage ----
    void hitBySolarFlare(double amount) {
        software -= amount;
        if (software < 0) software = 0;
    }
    void hitByBatteryDrain(double amount) {
        battery -= amount;
        if (battery < 0) battery = 0;
    }
    void hitByThrusterFailure() { thrusterOk = false; }

    // ---- shared diagnostic actions ----
    void autoPatch(ofstream &logFile, double simTime, string detail) {
        cout << ">> " << name << " software patched automatically (" << detail << ")\n";
        logFile << "[t=" << simTime << "s] [PATCH] " << name
                << " auto-patched: " << detail << "\n";
        software = 100;
    }

    void sendForRepair(ofstream &logFile, double simTime, string reason) {
        if (disabled) return;   // already sent, don't send twice
        disabled = true;
        repairTicksRemaining = REPAIR_DURATION_TICKS;
        cout << ">> " << name << " sent DISPATCH MANIFEST for physical repair (" << reason << ")\n";
        cout << "   Repair crew ETA: " << repairTicksRemaining << " ticks (back online around t="
             << (simTime + repairTicksRemaining) << "s)\n";
        logFile << "[t=" << simTime << "s] [DISPATCH] " << name
                << " needs repair. Reason: " << reason
                << " | Position(" << pos.x << "," << pos.y << "," << pos.z << ")"
                << " | Velocity(" << vel.x << "," << vel.y << "," << vel.z << ")"
                << " | Repair ETA: " << repairTicksRemaining << " ticks\n";
    }

    // ---- called every tick INSTEAD of moveOneStep()/runDiagnostics() while
    //      disabled - counts down the repair timer and brings the satellite
    //      back online once the repair crew finishes ----
    void tickRepair(ofstream &logFile, double simTime) {
        if (!disabled) return;

        if (repairTicksRemaining > 0) repairTicksRemaining--;

        if (repairTicksRemaining <= 0) {
            disabled = false;
            thrusterOk = true;
            software = 100;
            battery = 100;
            fuel += 25;
            if (fuel > 50) fuel = 50;       // repair crew tops up fuel, capped at the 50kg tank size

            cout << ">> " << name << " REPAIR COMPLETE - back ONLINE!\n";
            logFile << "[t=" << simTime << "s] [REPAIR] " << name << " repaired and back online\n";
        }
    }

    // ---- getters / setters ----
    int getId() { return id; }
    string getName() { return name; }
    Vector3 getPos() { return pos; }
    Vector3 getVel() { return vel; }
    double getBattery() { return battery; }
    double getSoftware() { return software; }
    double getFuel() { return fuel; }
    bool isThrusterOk() { return thrusterOk; }
    bool isDisabled() { return disabled; }
    int getRepairTicksRemaining() { return repairTicksRemaining; }
    void setPlanetName(string p) { planetName = p; }
    string getPlanetName() { return planetName; }
};


// ---- Derived class 1: Communication satellite ----
class CommunicationSatellite : public Satellite {
    double signal;   // percent, 0-100

public:
    CommunicationSatellite(int id_, string name_, Vector3 pos_, Vector3 vel_)
        : Satellite(id_, name_, pos_, vel_) {
        signal = 100;
    }

    string getType() override { return "Communication Satellite"; }

    void runDiagnostics(ofstream &logFile, double simTime) override {
        if (disabled) return;

        signal -= 0.05;   // signal slowly drifts down every tick
        if (signal < 0) signal = 0;

        if (software < 70) {
            autoPatch(logFile, simTime, "corrupted packet routing table");
        }
        if (signal < 40) {
            signal = 100;
            cout << ">> " << name << " transceiver re-tuned, signal back to 100%\n";
            logFile << "[t=" << simTime << "s] [PATCH] " << name << " signal re-tuned\n";
        }
        if (!thrusterOk) {
            sendForRepair(logFile, simTime, "thruster burnout");
        }
    }

    string getStatusLine() override {
        stringstream ss;
        ss << fixed << setprecision(1);
        ss << name << " [Communication Satellite] orbiting " << planetName;
        if (disabled) {
            ss << " -- ** UNDER REPAIR ** back online in " << repairTicksRemaining << " tick(s)";
        } else {
            ss << " | Batt=" << battery << "% (avg " << getAvgBattery() << "%)"
               << " SW=" << software << "% Signal=" << signal << "% Fuel=" << fuel << "kg"
               << " Thruster=" << (thrusterOk ? "OK" : "FAILED");
        }
        return ss.str();
    }
};


// ---- Derived class 2: Imaging satellite ----
class ImagingSatellite : public Satellite {
    double calibration;   // percent, 0-100

public:
    ImagingSatellite(int id_, string name_, Vector3 pos_, Vector3 vel_)
        : Satellite(id_, name_, pos_, vel_) {
        calibration = 100;
    }

    string getType() override { return "Imaging Satellite"; }

    void runDiagnostics(ofstream &logFile, double simTime) override {
        if (disabled) return;

        // low battery makes the camera stabilizer drift out of calibration faster
        double decay = (battery < 30) ? 0.5 : 0.05;
        calibration -= decay;
        if (calibration < 0) calibration = 0;

        if (software < 75) {
            autoPatch(logFile, simTime, "corrupted image compression logic");
        }
        if (calibration < 50) {
            calibration = 100;
            cout << ">> " << name << " sensor auto-recalibrated to 100%\n";
            logFile << "[t=" << simTime << "s] [PATCH] " << name << " sensor recalibrated\n";
        }
        if (!thrusterOk) {
            sendForRepair(logFile, simTime, "thruster burnout");
        }
    }

    string getStatusLine() override {
        stringstream ss;
        ss << fixed << setprecision(1);
        ss << name << " [Imaging Satellite] orbiting " << planetName;
        if (disabled) {
            ss << " -- ** UNDER REPAIR ** back online in " << repairTicksRemaining << " tick(s)";
        } else {
            ss << " | Batt=" << battery << "% (avg " << getAvgBattery() << "%)"
               << " SW=" << software << "% Calib=" << calibration << "% Fuel=" << fuel << "kg"
               << " Thruster=" << (thrusterOk ? "OK" : "FAILED");
        }
        return ss.str();
    }
};


/* =====================================================================
   8) SOLAR SYSTEM HELPERS -> these need the Satellite classes above to
      already exist, so they live down here instead of next to Planet.
   ===================================================================== */

// builds a satellite in a roughly circular orbit around the given planet.
// typeChoice: 1 = Communication Satellite, 2 = Imaging Satellite
Satellite* createSatelliteAroundPlanet(int id, string name, int typeChoice, Planet &planet) {
    double orbitRadius = 600 + rand() % 400;                 // 600-1000 units above the planet
    double angle = (rand() % 360) * 3.14159265 / 180.0;
    double speed = 6.5 + (rand() % 20) / 10.0;                // 6.5 - 8.5 units/sec, tangential

    Vector3 offset(orbitRadius * cos(angle), orbitRadius * sin(angle), (rand() % 200 - 100) / 10.0);
    Vector3 pos = planet.position + offset;

    // velocity perpendicular to the radius vector -> a roughly circular orbit
    Vector3 tangent(-sin(angle), cos(angle), 0);
    Vector3 vel = tangent * speed;

    Satellite *s;
    if (typeChoice == 1) {
        s = new CommunicationSatellite(id, name, pos, vel);
    } else {
        s = new ImagingSatellite(id, name, pos, vel);
    }
    s->setPlanetName(planet.name);
    return s;
}

// prints which satellites are orbiting which planet - answers "where is
// everything right now" at a glance
void printSolarSystemOverview(vector<Planet> &planets, vector<Satellite*> &satellites) {
    cout << "\n----- SOLAR SYSTEM OVERVIEW -----\n";
    for (int p = 0; p < (int)planets.size(); p++) {
        cout << planets[p].name << ": ";
        bool any = false;
        for (int i = 0; i < (int)satellites.size(); i++) {
            if (satellites[i]->getPlanetName() == planets[p].name) {
                if (any) cout << ", ";
                cout << satellites[i]->getName();
                any = true;
            }
        }
        if (!any) cout << "(no satellites)";
        cout << "\n";
    }
    cout << "----------------------------------\n\n";
}


/* =====================================================================
   9) CHAOS ENGINE -> randomly damages a satellite or spawns new debris
      each tick. chance(p) returns true roughly p% of the time.
   ===================================================================== */
bool chance(int percent) {
    return (rand() % 100) < percent;
}

void runChaosEngine(vector<Satellite*> &sats, vector<Debris> &debrisList,
                     int &nextDebrisId, ofstream &logFile, double simTime) {
    if (sats.empty()) return;

    Satellite *s = sats[rand() % sats.size()];

    // --- solar flare: corrupts software ---
    if (chance(6) && !s->isDisabled()) {
        double sev = 15 + rand() % 30;
        s->hitBySolarFlare(sev);
        cout << "** SOLAR FLARE ** hit " << s->getName() << ", software integrity -" << sev << "\n";
        logFile << "[t=" << simTime << "s] [CHAOS] Solar flare on " << s->getName()
                << " software -" << sev << "\n";
    }

    // --- battery degradation ---
    if (chance(7) && !s->isDisabled()) {
        double amt = 3 + rand() % 8;
        s->hitByBatteryDrain(amt);
        logFile << "[t=" << simTime << "s] [CHAOS] Battery drained on " << s->getName()
                << " by " << amt << "%\n";
    }

    // --- thruster burnout: hardware failure, can't be auto-fixed ---
    if (chance(2) && !s->isDisabled() && s->isThrusterOk()) {
        s->hitByThrusterFailure();
        cout << "!! THRUSTER FAILURE !! on " << s->getName() << "\n";
        logFile << "[t=" << simTime << "s] [CHAOS] Thruster burnout on " << s->getName() << "\n";
    }

    // --- spawn new debris near a random satellite ---
    if (chance(25)) {
        Vector3 base = s->getPos();
        Vector3 offset((rand() % 90 - 45), (rand() % 90 - 45), (rand() % 90 - 45));
        Vector3 spawnPos = base + offset;

        // aim the debris roughly toward the satellite, but give it the
        // SAME base velocity as the satellite too - otherwise the debris
        // gets left behind instantly since satellites move at ~7 units/sec
        // and the debris would only move at a fraction of that speed.
        Vector3 toSat = base - spawnPos;
        double mag = sqrt(toSat.x * toSat.x + toSat.y * toSat.y + toSat.z * toSat.z);
        Vector3 dir(0, 0, 1);
        if (mag > 0.001) dir = toSat * (1.0 / mag);

        double closingSpeed = 0.1 + (rand() % 20) / 100.0;
        Vector3 vel = s->getVel() + dir * closingSpeed;

        Debris d;
        d.id = nextDebrisId++;
        d.pos = spawnPos;
        d.vel = vel;
        debrisList.push_back(d);

        cout << "-- New debris #" << d.id << " spotted near " << s->getName() << "\n";
        logFile << "[t=" << simTime << "s] [CHAOS] New debris #" << d.id
                << " near " << s->getName() << "\n";
    }
}


/* =====================================================================
   10) MAIN -> asks the user for setup details, then runs the simulation
   ===================================================================== */
int main() {
    srand((unsigned) time(0));

    cout << "=====================================================\n";
    cout << " ORBITAL - Satellite Health & Collision Avoidance\n";
    cout << " (Solar System Edition)\n";
    cout << "=====================================================\n\n";

    // ---- build the solar system ----
    vector<Planet> planets = buildSolarSystem();

    cout << "Planets available to place satellites around:\n";
    for (int p = 0; p < (int)planets.size(); p++) {
        cout << "  " << (p + 1) << ". " << planets[p].name << "\n";
    }

    // ---- ask how many satellites, then details for each one ----
    int numSats;
    cout << "\nHow many satellites do you want to simulate? (1-6): ";
    cin >> numSats;
    if (numSats < 1) numSats = 1;
    if (numSats > 6) numSats = 6;

    vector<Satellite*> satellites;
    for (int i = 0; i < numSats; i++) {
        cout << "\n--- Satellite #" << (i + 1) << " setup ---\n";

        cout << "Enter a name for this satellite (no spaces): ";
        string name;
        cin >> name;

        int typeChoice;
        cout << "Choose satellite type:\n";
        cout << "  1. Communication Satellite\n";
        cout << "  2. Imaging Satellite\n";
        cout << "Your choice: ";
        cin >> typeChoice;
        if (typeChoice != 1 && typeChoice != 2) typeChoice = 1;

        int planetChoice;
        cout << "Choose which planet " << name << " should orbit (1-" << planets.size() << "): ";
        cin >> planetChoice;
        if (planetChoice < 1 || planetChoice > (int)planets.size()) planetChoice = 1;

        Satellite *s = createSatelliteAroundPlanet(i + 1, name, typeChoice, planets[planetChoice - 1]);
        satellites.push_back(s);
    }

    // ---- ask for the rest of the simulation settings ----
    int totalTicks;
    cout << "\nHow many ticks should the simulation run? (recommended 100-300): ";
    cin >> totalTicks;
    if (totalTicks < 10) totalTicks = 10;

    cout << "How many ticks should a repair crew take to fix a broken satellite? (recommended 15-30): ";
    cin >> REPAIR_DURATION_TICKS;
    if (REPAIR_DURATION_TICKS < 1) REPAIR_DURATION_TICKS = 15;

    int debrisPerSat;
    cout << "How much starting debris near each satellite? (recommended 4-8): ";
    cin >> debrisPerSat;
    if (debrisPerSat < 0) debrisPerSat = 0;
    if (debrisPerSat > 30) debrisPerSat = 30;

    printSolarSystemOverview(planets, satellites);

    ofstream logFile("blackbox.txt");
    logFile << "===== ORBITAL BLACKBOX LOG =====\n";

    // ---- seed some starting debris near each satellite ----
    vector<Debris> debrisList;
    int nextDebrisId = 1;
    for (int s = 0; s < (int)satellites.size(); s++) {
        Vector3 base = satellites[s]->getPos();
        Vector3 baseVel = satellites[s]->getVel();
        for (int i = 0; i < debrisPerSat; i++) {
            Debris d;
            d.id = nextDebrisId++;
            d.pos = base + Vector3(rand() % 120 - 60, rand() % 120 - 60, rand() % 120 - 60);
            // debris starts with roughly the same velocity as the nearby
            // satellite (co-orbital), plus a small random difference
            d.vel = baseVel + Vector3((rand() % 70 - 35) / 100.0,
                                       (rand() % 70 - 35) / 100.0,
                                       (rand() % 70 - 35) / 100.0);
            debrisList.push_back(d);
        }
    }

    double simTime = 0;
    double dt = 1.0;

    const double SAFE_DIST = 5.0;    // closer than this -> do an evasive burn
    const double WATCH_DIST = 15.0;  // closer than this -> worth tracking

    cout << "=====================================================\n";
    cout << " Running " << totalTicks << " ticks (dt=" << dt << "s), repair takes "
         << REPAIR_DURATION_TICKS << " ticks\n";
    cout << "=====================================================\n\n";

    for (int tick = 1; tick <= totalTicks; tick++) {
        simTime += dt;

        // 1) random hazard for this tick
        runChaosEngine(satellites, debrisList, nextDebrisId, logFile, simTime);

        // 2) move all debris
        for (int i = 0; i < (int)debrisList.size(); i++) {
            debrisList[i].pos = debrisList[i].pos + debrisList[i].vel * dt;
        }

        // 3) rebuild the AVL spatial index fresh every tick
        AVLTree tree;
        for (int i = 0; i < (int)debrisList.size(); i++) {
            tree.insert(getSectorKey(debrisList[i].pos), debrisList[i].id);
        }

        // 4) move satellites + run their own diagnostics
        //    (a disabled satellite doesn't move or run diagnostics - instead
        //    its repair countdown ticks down until it comes back online)
        for (int i = 0; i < (int)satellites.size(); i++) {
            if (satellites[i]->isDisabled()) {
                satellites[i]->tickRepair(logFile, simTime);
                continue;
            }
            satellites[i]->moveOneStep(dt);
            satellites[i]->runDiagnostics(logFile, simTime);
        }

        // 5) find nearby debris using the AVL tree, push into Min-Heap
        MinHeap heap;
        for (int i = 0; i < (int)satellites.size(); i++) {
            if (satellites[i]->isDisabled()) continue;
            Vector3 sp = satellites[i]->getPos();

            long long bx = (long long) floor(sp.x / 100.0);
            long long by = (long long) floor(sp.y / 100.0);
            long long bz = (long long) floor(sp.z / 100.0);

            // check the satellite's own sector + all 26 sectors around it
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        long long key = (bx + dx) * 1000000LL + (by + dy) * 1000LL + (bz + dz);
                        vector<int> ids = tree.search(key);

                        for (int k = 0; k < (int)ids.size(); k++) {
                            Debris *d = findDebrisById(debrisList, ids[k]);
                            if (d == NULL) continue;

                            double d3 = dist3D(sp, d->pos);
                            if (d3 < WATCH_DIST) {
                                Threat th;
                                th.satId = satellites[i]->getId();
                                th.debId = d->id;
                                th.distance = d3;
                                heap.push(th);
                            }
                        }
                    }
                }
            }
        }

        // 6) handle the most dangerous threats first (heap gives closest first)
        while (!heap.isEmpty()) {
            Threat th = heap.popMin();
            if (th.distance >= SAFE_DIST) continue;   // not close enough to react

            Satellite *sat = NULL;
            for (int i = 0; i < (int)satellites.size(); i++) {
                if (satellites[i]->getId() == th.satId) { sat = satellites[i]; break; }
            }
            if (sat == NULL || sat->isDisabled()) continue;

            Debris *d = findDebrisById(debrisList, th.debId);
            if (d == NULL) continue;

            cout << "!! PROXIMITY ALERT !! " << sat->getName() << " vs Debris#" << d->id
                 << " (distance=" << th.distance << " units)\n";
            logFile << "[t=" << simTime << "s] [ALERT] " << sat->getName()
                    << " close to Debris#" << d->id << " dist=" << th.distance << "\n";

            if (!sat->useFuel(2.5)) {
                sat->sendForRepair(logFile, simTime, "out of propellant during collision threat");
                continue;
            }

            // burn AWAY from the debris (opposite direction)
            Vector3 rel = d->pos - sat->getPos();
            double mag = sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
            Vector3 dir(0, 0, 1);
            if (mag > 0.001) dir = rel * (-1.0 / mag);
            Vector3 dv = dir * 0.08;

            sat->burn(dv);
            cout << ">> " << sat->getName() << " performed EVASIVE MANEUVER (fuel left="
                 << sat->getFuel() << "kg)\n";
            logFile << "[t=" << simTime << "s] [MANEUVER] " << sat->getName()
                    << " evasive burn executed\n";
        }

        // 7) print a status snapshot every 5 ticks (simple live dashboard)
        if (tick % 5 == 0 || tick == totalTicks) {
            cout << "\n----- STATUS at t=" << simTime << "s -----\n";
            for (int i = 0; i < (int)satellites.size(); i++) {
                cout << satellites[i]->getStatusLine() << "\n";
            }
            cout << "Debris tracked: " << debrisList.size()
                 << " | AVL tree nodes (sectors used): " << tree.countNodes() << "\n";
            cout << "-----------------------------------------\n\n";
        }

        usleep(60000);   // 60ms delay so it feels a bit "live" on screen
    }

    cout << "Simulation finished after " << simTime << " seconds.\n";
    cout << "Full event log saved in blackbox.txt\n";

    logFile << "===== END OF LOG =====\n";
    logFile.close();

    // clean up memory (we used 'new' for the satellites, so 'delete' them)
    for (int i = 0; i < (int)satellites.size(); i++) {
        delete satellites[i];
    }

    return 0;
}
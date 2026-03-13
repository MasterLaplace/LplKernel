#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <algorithm>

// ============================================================================
// PATTERNS DE CRÉATION (Creational)
// ============================================================================

namespace FactoryMethod {
    // Problème : Créer différents types de paquets réseau sans coupler le code.
    class NetworkPacket {
    public:
        virtual ~NetworkPacket() = default;
        virtual void Serialize() const = 0;
    };

    class SyncPacket : public NetworkPacket {
    public:
        void Serialize() const override { std::cout << "Sérialisation du paquet de synchronisation (Lockstep).\n"; }
    };

    class InputPacket : public NetworkPacket {
    public:
        void Serialize() const override { std::cout << "Sérialisation du paquet d'input neuronal.\n"; }
    };

    class PacketFactory {
    public:
        virtual ~PacketFactory() = default;
        virtual std::unique_ptr<NetworkPacket> CreatePacket() const = 0;
    };

    class SyncPacketFactory : public PacketFactory {
    public:
        std::unique_ptr<NetworkPacket> CreatePacket() const override { return std::make_unique<SyncPacket>(); }
    };
}

namespace AbstractFactory {
    // Problème : Instancier des sous-systèmes compatibles selon le hardware (PCVR vs FullDive).
    class IHapticFeedback { public: virtual void Pulse() = 0; virtual ~IHapticFeedback() = default; };
    class IRenderer { public: virtual void Render() = 0; virtual ~IRenderer() = default; };

    class FullDiveHaptics : public IHapticFeedback { public: void Pulse() override { std::cout << "Stimulation du cortex somatosensoriel.\n"; } };
    class FullDiveRenderer : public IRenderer { public: void Render() override { std::cout << "Injection directe dans le cortex visuel.\n"; } };

    class IHardwareFactory {
    public:
        virtual std::unique_ptr<IHapticFeedback> CreateHaptics() = 0;
        virtual std::unique_ptr<IRenderer> CreateRenderer() = 0;
        virtual ~IHardwareFactory() = default;
    };

    class FullDiveFactory : public IHardwareFactory {
    public:
        std::unique_ptr<IHapticFeedback> CreateHaptics() override { return std::make_unique<FullDiveHaptics>(); }
        std::unique_ptr<IRenderer> CreateRenderer() override { return std::make_unique<FullDiveRenderer>(); }
    };
}

namespace Builder {
    // Problème : Construire une entité complexe (Avatar) étape par étape.
    class Avatar {
    public:
        std::string mesh, neuralSignature, physicsBody;
        void Show() const { std::cout << "Avatar: " << mesh << " | Brain: " << neuralSignature << "\n"; }
    };

    class AvatarBuilder {
    private:
        Avatar avatar;
    public:
        AvatarBuilder& SetMesh(const std::string& m) { avatar.mesh = m; return *this; }
        AvatarBuilder& SetNeuralSignature(const std::string& ns) { avatar.neuralSignature = ns; return *this; }
        AvatarBuilder& SetPhysics(const std::string& p) { avatar.physicsBody = p; return *this; }
        Avatar Build() { return avatar; }
    };
}

namespace Prototype {
    // Problème : Copier un état de PNJ extrêmement lourd pour la prédiction sans le réinstancier.
    class SimEntity {
    public:
        virtual ~SimEntity() = default;
        virtual std::unique_ptr<SimEntity> Clone() const = 0;
        virtual void Info() const = 0;
    };

    class DeterministicNPC : public SimEntity {
        std::string aiState;
    public:
        DeterministicNPC(std::string state) : aiState(std::move(state)) {}
        std::unique_ptr<SimEntity> Clone() const override {
            return std::make_unique<DeterministicNPC>(*this); // Copie rapide
        }
        void Info() const override { std::cout << "NPC State: " << aiState << "\n"; }
    };
}

namespace Singleton {
    // Problème : Assurer une seule source de vérité pour l'horloge déterministe.
    class DeterministicClock {
    private:
        DeterministicClock() = default;
        uint64_t currentTick = 0;
    public:
        static DeterministicClock& Get() {
            static DeterministicClock instance; // Thread-safe en C++11
            return instance;
        }
        void AdvanceTick() { currentTick++; std::cout << "Tick: " << currentTick << "\n"; }
    };
}

// ============================================================================
// PATTERNS STRUCTURELS (Structural)
// ============================================================================

namespace Adapter {
    // Problème : Utiliser une vieille lib physique non-déterministe dans notre système strict.
    class LegacyPhysics {
    public:
        void FloatCalculate() { std::cout << "Calcul flottant instable...\n"; }
    };

    class IDeterministicCollider {
    public:
        virtual void FixedPointCalculate() = 0;
        virtual ~IDeterministicCollider() = default;
    };

    class PhysicsAdapter : public IDeterministicCollider {
    private:
        LegacyPhysics legacySys;
    public:
        void FixedPointCalculate() override {
            std::cout << "Conversion des floats en fixed-point (Adapter). ";
            legacySys.FloatCalculate();
        }
    };
}

namespace Bridge {
    // Problème : Séparer l'abstraction de l'interface neuronale de son implémentation hardware.
    class INeuroDriver {
    public:
        virtual void SendSignals(const std::string& data) = 0;
        virtual ~INeuroDriver() = default;
    };

    class NeuralLinkHardware : public INeuroDriver {
    public:
        void SendSignals(const std::string& data) override { std::cout << "Hardware NeuralLink v2 envoie: " << data << "\n"; }
    };

    class BrainComputerInterface {
    protected:
        std::unique_ptr<INeuroDriver> driver;
    public:
        BrainComputerInterface(std::unique_ptr<INeuroDriver> d) : driver(std::move(d)) {}
        virtual void TriggerPain(int level) = 0;
        virtual ~BrainComputerInterface() = default;
    };

    class AdvancedBCI : public BrainComputerInterface {
    public:
        AdvancedBCI(std::unique_ptr<INeuroDriver> d) : BrainComputerInterface(std::move(d)) {}
        void TriggerPain(int level) override { driver->SendSignals("PAIN_LEVEL_" + std::to_string(level)); }
    };
}

namespace Composite {
    // Problème : Gérer un arbre spatial (Octree/Scene Graph) de la même manière qu'un objet unique.
    class SpatialNode {
    public:
        virtual void UpdateVisibility() = 0;
        virtual ~SpatialNode() = default;
    };

    class EntityNode : public SpatialNode {
    public:
        void UpdateVisibility() override { std::cout << "  Culling de l'entite.\n"; }
    };

    class RegionNode : public SpatialNode {
    private:
        std::vector<std::unique_ptr<SpatialNode>> children;
    public:
        void Add(std::unique_ptr<SpatialNode> node) { children.push_back(std::move(node)); }
        void UpdateVisibility() override {
            std::cout << "Mise a jour de la region:\n";
            for (auto& child : children) child->UpdateVisibility();
        }
    };
}

namespace Decorator {
    // Problème : Ajouter compression et chiffrement aux flux réseaux dynamiquement.
    class Stream {
    public:
        virtual void Write(const std::string& data) = 0;
        virtual ~Stream() = default;
    };

    class NetworkStream : public Stream {
    public:
        void Write(const std::string& data) override { std::cout << "Envoi raw: " << data << "\n"; }
    };

    class StreamDecorator : public Stream {
    protected:
        std::unique_ptr<Stream> wrapped;
    public:
        StreamDecorator(std::unique_ptr<Stream> s) : wrapped(std::move(s)) {}
        void Write(const std::string& data) override { wrapped->Write(data); }
    };

    class EncryptedStream : public StreamDecorator {
    public:
        EncryptedStream(std::unique_ptr<Stream> s) : StreamDecorator(std::move(s)) {}
        void Write(const std::string& data) override { 
            std::cout << "[Chiffrement AES-256] "; 
            StreamDecorator::Write(data); 
        }
    };
}

namespace Facade {
    // Problème : Offrir une API simple pour l'initialisation du Kernel.
    class PhysicsKernel { public: void InitFixedPoint() { std::cout << "Physique prête.\n"; } };
    class RenderKernel { public: void InitVulkan() { std::cout << "Rendu prêt.\n"; } };
    class NetKernel { public: void BindSockets() { std::cout << "Réseau prêt.\n"; } };

    class EngineFacade {
    private:
        PhysicsKernel phys; RenderKernel rend; NetKernel net;
    public:
        void BootSequence() {
            phys.InitFixedPoint();
            rend.InitVulkan();
            net.BindSockets();
            std::cout << "FullDive Engine Booted.\n";
        }
    };
}

namespace Flyweight {
    // Problème : Rendre des millions d'arbres/voxels en ne stockant le mesh qu'une seule fois.
    class VoxelMesh {
    public:
        std::string geometry = "LOURDE GEOMETRIE";
    };

    class VoxelInstance {
    private:
        VoxelMesh* sharedMesh; // Pointeur partagé
        float x, y, z;
    public:
        VoxelInstance(VoxelMesh* m, float x, float y, float z) : sharedMesh(m), x(x), y(y), z(z) {}
        void Render() { std::cout << "Voxel en " << x << "," << y << " via " << sharedMesh->geometry << "\n"; }
    };
}

namespace Proxy {
    // Problème : Représenter un joueur distant avec prédiction de mouvement pour cacher la latence.
    class IPlayer {
    public:
        virtual void Move() = 0;
        virtual ~IPlayer() = default;
    };

    class RemotePlayer : public IPlayer {
    public:
        void Move() override { std::cout << "Déplacement validé par le serveur.\n"; }
    };

    class PlayerPredictionProxy : public IPlayer {
    private:
        RemotePlayer realPlayer;
    public:
        void Move() override {
            std::cout << "[Proxy] Interpolation locale (Dead Reckoning) en attendant le serveur...\n";
            realPlayer.Move();
        }
    };
}

// ============================================================================
// PATTERNS COMPORTEMENTAUX (Behavioral)
// ============================================================================

namespace ChainOfResponsibility {
    // Problème : Gérer les signaux d'un casque neuronal (Sécurité vitale -> UI -> Jeu).
    class NeuralHandler {
    protected:
        std::shared_ptr<NeuralHandler> next;
    public:
        void SetNext(std::shared_ptr<NeuralHandler> n) { next = n; }
        virtual void HandleSignal(int dangerLevel) { if (next) next->HandleSignal(dangerLevel); }
        virtual ~NeuralHandler() = default;
    };

    class SafetyLimiter : public NeuralHandler {
    public:
        void HandleSignal(int dangerLevel) override {
            if (dangerLevel > 90) std::cout << "URGENCE : Déconnexion vitale du joueur !\n";
            else NeuralHandler::HandleSignal(dangerLevel);
        }
    };

    class GameInput : public NeuralHandler {
    public:
        void HandleSignal(int dangerLevel) override { std::cout << "Signal transmis au moteur de jeu.\n"; }
    };
}

namespace Command {
    // Problème : Encapsuler un input pour le Rollback Netcode / Deterministic Lockstep.
    class ICommand {
    public:
        virtual void Execute() = 0;
        virtual ~ICommand() = default;
    };

    class MoveCommand : public ICommand {
        int x, y;
    public:
        MoveCommand(int x, int y) : x(x), y(y) {}
        void Execute() override { std::cout << "Command: Move to " << x << "," << y << "\n"; }
    };
}

namespace Iterator {
    // Problème : Itérer sur les entités d'une grille spatiale sans exposer sa structure interne complexe.
    class SpatialGrid {
        std::vector<std::string> entities = {"Joueur1", "MonstreA", "Arbre"};
    public:
        class Iter {
            std::vector<std::string>::iterator current;
        public:
            Iter(std::vector<std::string>::iterator start) : current(start) {}
            std::string& operator*() { return *current; }
            Iter& operator++() { current++; return *this; }
            bool operator!=(const Iter& other) const { return current != other.current; }
        };
        Iter begin() { return Iter(entities.begin()); }
        Iter end() { return Iter(entities.end()); }
    };
}

namespace Mediator {
    // Problème : Eviter que le système de collisions ne connaisse directement le système audio.
    class EventMediator {
    public:
        virtual void Notify(const std::string& event) = 0;
        virtual ~EventMediator() = default;
    };

    class AudioSystem {
    public:
        void PlayCrash() { std::cout << "Audio: SBANG !\n"; }
    };

    class PhysicsSystem {
        EventMediator* med;
    public:
        PhysicsSystem(EventMediator* m) : med(m) {}
        void OnCollision() { 
            std::cout << "Physique: Collision detectee.\n"; 
            med->Notify("Collision"); 
        }
    };

    class KernelHub : public EventMediator {
        AudioSystem audio;
    public:
        void Notify(const std::string& event) override {
            if (event == "Collision") audio.PlayCrash();
        }
    };
}

namespace Memento {
    // Problème : Sauvegarder l'état entier de la frame 45 pour un éventuel Rollback si un paquet arrive en retard.
    class FrameSnapshot { // Memento
    private:
        std::string state;
        friend class GameSimulation;
        FrameSnapshot(std::string s) : state(std::move(s)) {}
    };

    class GameSimulation { // Originator
    private:
        std::string currentState;
    public:
        void SetState(std::string s) { currentState = std::move(s); std::cout << "Etat = " << currentState << "\n"; }
        FrameSnapshot Save() { return FrameSnapshot(currentState); }
        void Rollback(const FrameSnapshot& snap) { currentState = snap.state; std::cout << "Rollback vers " << currentState << "\n"; }
    };
}

namespace Observer {
    // Problème : Avertir l'UI quand le rythme cardiaque neuronal augmente.
    class IObserver { public: virtual void OnStressSpike() = 0; virtual ~IObserver() = default; };

    class PlayerVitals {
        std::vector<IObserver*> observers;
    public:
        void AddObserver(IObserver* o) { observers.push_back(o); }
        void HeartRateSpike() {
            std::cout << "Rythme cardiaque en hausse !\n";
            for (auto* o : observers) o->OnStressSpike();
        }
    };

    class DangerUI : public IObserver {
    public:
        void OnStressSpike() override { std::cout << "UI: Affichage du contour rouge à l'écran.\n"; }
    };
}

namespace State {
    // Problème : Gérer la machine à états de la connexion cérébrale.
    class BCIContext;
    class BCIState {
    public:
        virtual void Update(BCIContext* ctx) = 0;
        virtual ~BCIState() = default;
    };

    class BCIContext {
        std::unique_ptr<BCIState> state;
    public:
        void SetState(std::unique_ptr<BCIState> s) { state = std::move(s); }
        void Update() { if(state) state->Update(this); }
    };

    class ActiveDiveState : public BCIState {
    public:
        void Update(BCIContext*) override { std::cout << "État: Plongée neurale active (FullDive).\n"; }
    };

    class CalibratingState : public BCIState {
    public:
        void Update(BCIContext* ctx) override {
            std::cout << "État: Calibration terminée, passage en FullDive.\n";
            ctx->SetState(std::make_unique<ActiveDiveState>());
        }
    };
}

namespace Strategy {
    // Problème : Changer d'algo de prédiction de position selon la bande passante.
    class IPredictionAlgo { public: virtual void Predict() = 0; virtual ~IPredictionAlgo() = default; };

    class DeadReckoning : public IPredictionAlgo { public: void Predict() override { std::cout << "Prédiction linéaire basique.\n"; } };
    class HermiteSpline : public IPredictionAlgo { public: void Predict() override { std::cout << "Prédiction courbe Hermite précise.\n"; } };

    class NetworkGhost {
        std::unique_ptr<IPredictionAlgo> algo;
    public:
        void SetAlgo(std::unique_ptr<IPredictionAlgo> a) { algo = std::move(a); }
        void Update() { algo->Predict(); }
    };
}

namespace TemplateMethod {
    // Problème : Garantir que toutes les phases de la boucle déterministe respectent l'ordre strict.
    class EngineTick {
    protected:
        virtual void ReadInputs() = 0;
        virtual void SimulateFixedStep() = 0;
    public:
        void ExecuteTick() { // Le Template Method
            ReadInputs();
            SimulateFixedStep();
            std::cout << "Post-Tick Validation.\n";
        }
        virtual ~EngineTick() = default;
    };

    class ServerTick : public EngineTick {
    protected:
        void ReadInputs() override { std::cout << "Lecture des paquets clients.\n"; }
        void SimulateFixedStep() override { std::cout << "Simulation physique serveur.\n"; }
    };
}

namespace Visitor {
    // Problème : Sérialiser différents types d'entités sans salir leurs classes métiers.
    class Player; class Prop;
    class IVisitor {
    public:
        virtual void Visit(Player& p) = 0;
        virtual void Visit(Prop& p) = 0;
        virtual ~IVisitor() = default;
    };

    class IEntity { public: virtual void Accept(IVisitor& v) = 0; virtual ~IEntity() = default; };

    class Player : public IEntity { public: void Accept(IVisitor& v) override { v.Visit(*this); } };
    class Prop : public IEntity { public: void Accept(IVisitor& v) override { v.Visit(*this); } };

    class NetworkSerializer : public IVisitor {
    public:
        void Visit(Player&) override { std::cout << "Sérialisation des inputs vitaux du Joueur.\n"; }
        void Visit(Prop&) override { std::cout << "Sérialisation de la physique du Prop.\n"; }
    };
}

// ============================================================================
// BONUS PATTERNS : SPÉCIFIQUE MOTEUR TEMPS RÉEL (Game Programming Patterns)
// ============================================================================

namespace ObjectPool {
    // Problème : Allouer des sorts/balles dynamiquement pendant un tick provoque du lag.
    class Particle {
    public:
        bool active = false;
        void Spawn() { active = true; std::cout << "Particule recyclée sans allocation mémoire !\n"; }
    };

    class ParticlePool {
    private:
        std::vector<Particle> pool;
    public:
        ParticlePool(size_t size) : pool(size) {}
        void Fire() {
            for (auto& p : pool) {
                if (!p.active) { p.Spawn(); return; }
            }
        }
    };
}

namespace DoubleBuffer {
    // Problème : Lire et écrire les positions en même temps crée des incohérences (Tearing multithread).
    class Transform {
    private:
        float states[2];
        int current = 0;
    public:
        Transform() { states[0] = 0.0f; states[1] = 0.0f; }
        void WriteNext(float v) { states[1 - current] = v; }
        float ReadCurrent() const { return states[current]; }
        void Swap() { current = 1 - current; std::cout << "Swap buffer. Pos: " << ReadCurrent() << "\n"; }
    };
}


// ============================================================================
// POINT D'ENTRÉE : DÉMONSTRATION COMPILABLE
// ============================================================================

int main() {
    std::cout << "--- DEMARRAGE DU KERNEL FULLDIVE ---\n";

    Facade::EngineFacade kernel;
    kernel.BootSequence();

    std::cout << "\n--- TEST DES PATTERNS ---\n";

    // Singleton
    Singleton::DeterministicClock::Get().AdvanceTick();

    // Command
    Command::MoveCommand cmd(10, 20);
    cmd.Execute();

    // Strategy
    Strategy::NetworkGhost ghost;
    ghost.SetAlgo(std::make_unique<Strategy::HermiteSpline>());
    ghost.Update();

    // Memento
    Memento::GameSimulation sim;
    sim.SetState("Frame 102");
    auto snapshot = sim.Save();
    sim.SetState("Frame 105 (Corrompue)");
    sim.Rollback(snapshot);

    // Object Pool
    ObjectPool::ParticlePool pPool(10);
    pPool.Fire();

    // Double Buffer
    DoubleBuffer::Transform trans;
    trans.WriteNext(55.0f);
    trans.Swap();

    std::cout << "\n--- TOUS LES SOUS-SYSTEMES SONT FONCTIONNELS ---\n";
    return 0;
}

#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <map>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	//mesh transforms
	Scene::Transform *ghost = nullptr;
	Scene::Transform *door = nullptr;
	Scene::Transform *key_1 = nullptr;
	Scene::Transform *key_2 = nullptr;
	Scene::Transform *key_3 = nullptr;

	//ghost location
	glm::vec3 ghost_position;

	//tree x and y positions
	std::map<float, float> tree_positions;

	//game state trackers
	uint8_t keys = 0;
	uint8_t key_1_picked = 0;
	uint8_t key_2_picked = 0;
	uint8_t key_3_picked = 0;
	uint8_t door_contact = 0;
	uint8_t game_over = 0;

	double pi = 3.1415926535; // Constant for my own convenience

	//looped sounds coming from the ghost's location
	std::shared_ptr<Sound::PlayingSample> stab_ghost_loop;
	std::shared_ptr<Sound::PlayingSample> stab_loop;
	
	//camera:
	Scene::Camera *camera = nullptr;

};

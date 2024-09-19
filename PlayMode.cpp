#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <math.h>

GLuint meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > map_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("forest_map.pnct"));
	meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > map_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("forest_map.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = map_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});

Load<Sound::Sample> pickup_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("pickup.wav"));
});

Load<Sound::Sample> win_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("win.wav"));
});

Load<Sound::Sample> locked_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("locked.wav"));
});

Load<Sound::Sample> stab_ghost_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("stab-ghost.wav"));
});

Load<Sound::Sample> stab_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("stab.wav"));
});

PlayMode::PlayMode() : scene(*map_scene) {
	//get pointers and sort tree positions for convenience:
	for (auto &transform : scene.transforms) {
		if (transform.name == "Camera" || transform.name == "Sky" || transform.name == "Ground" || transform.name == "Wall") continue;
		else if (transform.name == "Ghost") ghost = &transform;
		else if (transform.name == "Door") door = &transform;
		else if (transform.name == "Key") key_1 = &transform;
		else if (transform.name == "Key.001") key_2 = &transform;
		else if (transform.name == "Key.002") key_3 = &transform;
		else tree_positions[transform.position.x] = transform.position.y;
	}

	//get stored values
	ghost_position = ghost->position;
	
	//hide ghost
	ghost->position.z = -15.0f;

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//start sound loop playing:
	// (note: position will be over-ridden in update())
	stab_ghost_loop = Sound::loop_3D(*stab_ghost_sample, 1.0f, ghost_position, 10.0f);
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			
			glm::quat curr_rotation = camera->transform->rotation;
			glm::vec3 eulers = glm::eulerAngles(curr_rotation);

			// Apply yaw
			eulers.z += -motion.x * camera->fovy;

			// Apply pitch and lock it to front of camera
			eulers.x += motion.y * camera->fovy;
			if (eulers.x > (pi - 0.1f)) eulers.x = float(pi - 0.1f);
			else if (eulers.x < 0.0f) eulers.x = 0.0f;

			camera->transform->rotation = glm::normalize(glm::quat(eulers));
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	//check for game over
	if (game_over) return;

	//move sound to follow ghost position:
	stab_ghost_loop->set_position(ghost_position, 1.0f / 60.0f);

	//move camera and check for collisions:
	{
		//combine inputs into a move:
		constexpr float PlayerSpeed = 10.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 frame_forward = -frame[2];

		glm::vec3 old_position = camera->transform->position;
		camera->transform->position += move.x * frame_right + move.y * frame_forward;
		camera->transform->position.z = 2.0f;

		//check for tree collisions
		std::pair<const float, float> lower = *tree_positions.lower_bound(camera->transform->position.x);
		std::pair<const float, float> upper = *tree_positions.upper_bound(camera->transform->position.x);
		if (std::abs(camera->transform->position.x - lower.first) <= 1.5f && std::abs(old_position.y - lower.second) <= 1.5f)
			camera->transform->position.x = old_position.x;
		if (std::abs(camera->transform->position.y - lower.second) <= 1.5f && std::abs(old_position.x - lower.first) <= 1.5f)
			camera->transform->position.y = old_position.y;
		if (std::abs(camera->transform->position.x - upper.first) <= 1.5f && std::abs(old_position.y - upper.second) <= 1.5f)
			camera->transform->position.x = old_position.x;
		if (std::abs(camera->transform->position.y - upper.second) <= 1.5f && std::abs(old_position.x - upper.first) <= 1.5f)
			camera->transform->position.y = old_position.y;

		//check for wall collisions
		if (camera->transform->position.x >= 99.0f) camera->transform->position.x = old_position.x;
		if (camera->transform->position.x <= -99.0f) camera->transform->position.x = old_position.x;
		if (camera->transform->position.y >= 99.0f) camera->transform->position.y = old_position.y;
		if (camera->transform->position.y <= -99.0f) camera->transform->position.y = old_position.y;

		//check for key collisions
		if ((! key_1_picked) && std::abs(camera->transform->position.x - key_1->position.x) <= 1.0f && std::abs(camera->transform->position.y - key_1->position.y) <= 1.0f) {
			key_1_picked = true;
			keys += 1;
			key_1->position.z = -15.0f;
			Sound::play(*pickup_sample);
		}
		if ((! key_2_picked) && std::abs(camera->transform->position.x - key_2->position.x) <= 1.0f && std::abs(camera->transform->position.y - key_2->position.y) <= 1.0f) {
			key_2_picked = true;
			keys += 1;
			key_2->position.z = -15.0f;
			Sound::play(*pickup_sample);
		}
		if ((! key_3_picked) && std::abs(camera->transform->position.x - key_3->position.x) <= 1.0f && std::abs(camera->transform->position.y - key_3->position.y) <= 1.0f) {
			key_3_picked = true;
			keys += 1;
			key_3->position.z = -15.0f;
			Sound::play(*pickup_sample);
		}

		//check for door collisions
		if (camera->transform->position.x >= 98.0f && std::abs(camera->transform->position.y - door->position.y) <= 3.0f) {
			if (keys >= 3) {
				game_over = true;
				std::cout << "You win!" << std::endl;
				stab_ghost_loop->stop(0.0f);
				Sound::play(*win_sample);
			} else if (! door_contact) {
				door_contact = true;
				Sound::play(*locked_sample);
			}
		} else door_contact = false;
	}

	//make ghost visible if close to player
	float x_distance = std::abs(ghost_position.x - camera->transform->position.x);
	float y_distance = std::abs(ghost_position.y - camera->transform->position.y);
	if (x_distance <= 5.0f && y_distance <= 5.0f) ghost->position = ghost_position;
	else ghost->position.z = -15.0f;

	//stab player if ghost is too close
	if (x_distance <= 2.0f && y_distance <= 2.0f) {
		game_over = true;
		std::cout << "Game over. You got stabbed." << std::endl;
		stab_ghost_loop->stop(0.0f);
		stab_loop = Sound::loop_3D(*stab_sample, 0.75f, camera->transform->position, 10.0f);
	}

	//shift ghost based on player location and distance
	float x_shift = 0.0f;
	float y_shift = 0.0f;
	if (ghost_position.x < camera->transform->position.x) x_shift = x_distance * elapsed / 1.5f;
	else if (ghost_position.x > camera->transform->position.x) x_shift = -x_distance * elapsed / 1.5f;
	if (ghost_position.y < camera->transform->position.y) y_shift = y_distance * elapsed / 1.5f;
	else if (ghost_position.y > camera->transform->position.y) y_shift = -y_distance * elapsed / 1.5f;
	ghost_position += glm::vec3(x_shift, y_shift, 0.0f);

	//rotate ghost to player
	double x_diff = camera->transform->position.x - ghost->position.x;
	double y_diff = camera->transform->position.y - ghost->position.y;
	double angle = atan2(y_diff, x_diff); // Note: right of ghost is angle 0, need to subtract 90 degrees
	ghost->rotation = glm::quat(glm::vec3(0.0f, 0.0f, angle - (pi / 2.0f)));

	{ //update listener to camera position:
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 frame_right = frame[0];
		glm::vec3 frame_at = frame[3];
		Sound::listener.set_position_right(frame_at, frame_right, 1.0f / 60.0f);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("Mouse rotates camera; WASD moves; escape ungrabs mouse. Keys: " + std::to_string(keys) + "/3",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Mouse rotates camera; WASD moves; escape ungrabs mouse. Keys: " + std::to_string(keys) + "/3",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
	GL_ERRORS();
}

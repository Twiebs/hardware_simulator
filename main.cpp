#define QUICKAPP_IMPLEMENTATION
#define QUICKAPP_IMGUI
#define QUICKAPP_RENDER
#include "QuickApp.h"

#include "imgui_node_graph_test.cpp"

template<typename T>
struct DynamicArray {
  size_t capacity;
  size_t count;
  T *data;

  inline T& operator[](const size_t i){
    assert(i < count);
    return data[i];
  }
};

template<typename T>
void ArrayAdd(const T& t, DynamicArray<T>& array){
  if(array.count + 1 > array.capacity){
    array.capacity = array.capacity + 10;
    if(array.data != 0) array.data = (T*)realloc(array.data, array.capacity);
    else array.data = (T *)malloc(sizeof(T) * array.capacity);
  }

  array.data[array.count] = t;
  array.count += 1;
}

template<typename T>
void ArrayRemoveAtIndexUnordered(const size_t index, DynamicArray<T>& array){
  assert(index < array.count);
  array.data[index] = array.data[array.count-1];
  array.count -= 1;
}

enum LogicNodeType {
  LogicNodeType_AND,
  LogicNodeType_OR,
  LogicNodeType_XOR,
  LogicNodeType_SIGNAL,
  LogicNodeType_STATE,
  LogicNodeType_COUNT,
};

static const char *LogicNodeName[] = {
  "AND",
  "OR",
  "XOR",
  "SIGNAL",
  "STATE",
};

struct NodeConnection {
  uint32_t node_index;
  uint32_t input_index;
};

//NOTE(Torin) This EditorNode is used to represent nodes only within the editor
//Its implementation is simple and naive inorder to simplify the editor code as much as possible
//and allow users to modify the data it represents as easily as possible.  When the simulator is run
//editor nodes are 'compiled' into SimulatorNodes which are much more efficent but are immuatable
struct EditorNode {
  LogicNodeType type;
  size_t input_count;
  size_t output_count;

  ImVec2 position;
  ImVec2 size;

  uint8_t input_was_set[2];
  uint8_t input_state[2];
  //TODO(Torin) This should really be a DArray of DArrays for outputConections
  DynamicArray<NodeConnection> outputConnections;

  uint8_t signal_state;
  ImVec2 GetInputSlotPos(int slot_no) const   { return ImVec2(position.x, position.y + size.y * ((float)slot_no+1) / ((float)2+1)); }
  ImVec2 GetOutputSlotPos(int slot_no) const  { return ImVec2(position.x + size.x, position.y + size.y * ((float)slot_no+1) / ((float)1+1)); }
};

struct Editor {
  DynamicArray<EditorNode> nodes;
};

#define INVALID_NODE_ID UINT32_MAX

EditorNode* CreateNode(LogicNodeType type, Editor *editor){
  EditorNode node = {};
  node.type = type;
  switch(node.type){

    case LogicNodeType_STATE: {
      node.input_count = 1;
      node.output_count = 0;
    }break;

    case LogicNodeType_SIGNAL:{
      node.input_count = 0;
      node.output_count = 1;
    } break;

    default: {
      node.input_count = 2;
      node.output_count = 1;
    } break;
  }


  ArrayAdd(node, editor->nodes);
  return &editor->nodes[editor->nodes.count-1];
}

static inline void SimulateNode(EditorNode *node, Editor *editor);

static inline
void TransmitOutputStateAndSimulateConnectedNodes(EditorNode *node, uint8_t outputState, Editor *editor){
  for(size_t i = 0; i < node->outputConnections.count; i++){
    auto &connection = node->outputConnections[i];
    EditorNode *connectedNode = &editor->nodes[connection.node_index];
    connectedNode->input_state[connection.input_index] = outputState;
    connectedNode->input_was_set[connection.input_index] = 1;
    SimulateNode(connectedNode, editor);
  }
}

static inline
void SimulateNode(EditorNode *node, Editor *editor){
  switch(node->type){
    case LogicNodeType_SIGNAL: {
      for(size_t connectionIndex = 0; connectionIndex < node->outputConnections.count; connectionIndex++){
        auto& connection = node->outputConnections[connectionIndex];
        EditorNode *connectedNode = &editor->nodes[node->outputConnections[connectionIndex].node_index];
        connectedNode->input_state[connection.input_index] = node->signal_state;
        connectedNode->input_was_set[connection.input_index] = 1;
      }
    }break;

    case LogicNodeType_AND:{
      if(node->input_was_set[0] == 0) return;
      if(node->input_was_set[1] == 0) return;
      uint8_t output_signal = node->input_state[0] && node->input_state[1];
      TransmitOutputStateAndSimulateConnectedNodes(node, output_signal, editor);
    } break;

    case LogicNodeType_OR:{
      if(node->input_was_set[0] == 0) return;
      if(node->input_was_set[1] == 0) return;
      uint8_t output_signal = node->input_state[0] || node->input_state[1];
      TransmitOutputStateAndSimulateConnectedNodes(node, output_signal, editor);
    }break;

    case LogicNodeType_XOR:{
      if(node->input_was_set[0] == 0) return;
      if(node->input_was_set[1] == 0) return;
      uint8_t output_signal = node->input_state[0] || node->input_state[1];
      output_signal = output_signal && !(node->input_state[0] && node->input_state[1]);
      TransmitOutputStateAndSimulateConnectedNodes(node, output_signal, editor);
    }break;


    case LogicNodeType_STATE: {
      if(node->input_was_set[0] == 1){
        node->signal_state = node->input_state[0];
      }
    }break;

  }
}

static inline
void SimulationStep(Editor *editor){
  for(size_t rootNodeIndex = 0; rootNodeIndex < editor->nodes.count; rootNodeIndex++){
    EditorNode *node = &editor->nodes[rootNodeIndex];
    SimulateNode(node, editor);
  }
}

static inline
void DrawEditorNode(EditorNode *node){
  switch(node->type){
    case LogicNodeType_SIGNAL:{
      const char *text = node->signal_state ? "1" : "0";
      if(ImGui::Button(text, ImVec2(32, 32))){
        node->signal_state = !node->signal_state;
      }
    }break;

    case LogicNodeType_STATE:{
      const char *text = node->signal_state ? "1" : "0";
      ImGui::Text(text);
    }break;

    default:{
      ImGui::Text(LogicNodeName[node->type]);
    } break;
  }
}

void DrawEditor(Editor *editor){
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
  ImGui::SetNextWindowSize(ImVec2(1280, 720));
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  if(!ImGui::Begin("Example: Custom Node Graph", 0, flags)){
    ImGui::End();
    return;
  }

    static ImVec2 scrolling = ImVec2(0.0f, 0.0f);
    static bool show_grid = true;
    static int node_selected = -1;

    // Draw a list of nodes on the left side
    bool open_context_menu = false;
    int node_hovered_in_list = -1;
    int node_hovered_in_scene = -1;
    ImGui::BeginChild("node_list", ImVec2(100,0));
    ImGui::Text("Nodes");
    ImGui::Separator();

    for (int node_idx = 0; node_idx < editor->nodes.count; node_idx++){
      ImGui::PushID(node_idx);
      EditorNode *node = &editor->nodes[node_idx];
      if(ImGui::Selectable(LogicNodeName[node->type], node_idx == node_selected)){
        node_selected = node_idx;
      }
        
      if (ImGui::IsItemHovered()){
        node_hovered_in_list = node_idx;
        open_context_menu |= ImGui::IsMouseClicked(1);
      }
      ImGui::PopID();  
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();

    const float NODE_SLOT_RADIUS = 8.0f;
    const ImVec2 NODE_WINDOW_PADDING(16.0f, 16.0f);

    // Create our child canvas
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1,1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(60,60,70,200));
    ImGui::BeginChild("scrolling_region", ImVec2(0,0), true, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoMove);
    ImGui::PushItemWidth(120.0f);

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImVec2 offset = ImGui::GetCursorScreenPos() - scrolling;
    ImVec2 canvasMouseCoords = ImGui::GetMousePos() - canvasOrigin;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(2);

    // Display grid
    if(show_grid){
      ImU32 GRID_COLOR = ImColor(200,200,200,40);
      float GRID_SZ = 64.0f;
      ImVec2 win_pos = ImGui::GetCursorScreenPos();
      ImVec2 canvas_sz = ImGui::GetWindowSize();
      for(float x = fmodf(offset.x,GRID_SZ); x < canvas_sz.x; x += GRID_SZ)
        draw_list->AddLine(ImVec2(x,0.0f)+win_pos, ImVec2(x,canvas_sz.y)+win_pos, GRID_COLOR);
      for(float y = fmodf(offset.y,GRID_SZ); y < canvas_sz.y; y += GRID_SZ)
        draw_list->AddLine(ImVec2(0.0f,y)+win_pos, ImVec2(canvas_sz.x,y)+win_pos, GRID_COLOR);
    }

    // Display links
    draw_list->ChannelsSetCurrent(0); // Background
    for(size_t i = 0; i < editor->nodes.count; i++){
      EditorNode *a = &editor->nodes[i];
      for(size_t n = 0; n < a->outputConnections.count; n++){
        ImVec2 p1 = offset + a->GetOutputSlotPos(0);
        EditorNode *b = &editor->nodes[a->outputConnections[n].node_index];
        ImVec2 p2 = offset + b->GetInputSlotPos(a->outputConnections[n].input_index);
        draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
      }
    }

    static int dragNodeIndex = -1;
    static int dragSlotIndex = -1;




    // Draw nodes
    for(size_t i = 0; i < editor->nodes.count; i++){
      ImGui::PushID(i);
      EditorNode *node = &editor->nodes[i];
      
      ImVec2 node_rect_min = offset + node->position;
      draw_list->ChannelsSetCurrent(1); // Foreground
      ImGui::SetCursorScreenPos(node_rect_min + NODE_WINDOW_PADDING);
      ImGui::BeginGroup(); // Lock horizontal position
      DrawEditorNode(node);
      ImGui::EndGroup();

      node->size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
      ImVec2 node_rect_max = node_rect_min + node->size;

      // Display node box
      draw_list->ChannelsSetCurrent(0); // Background
      ImGui::SetCursorScreenPos(node_rect_min);
      ImGui::InvisibleButton("node", node->size);
      
      if(ImGui::IsItemHovered()){
        node_hovered_in_scene = i;
        open_context_menu |= ImGui::IsMouseClicked(1);
      }

      bool node_moving_active = ImGui::IsItemActive();
      if(node_moving_active)
        node_selected = i;
      if(node_moving_active && ImGui::IsMouseDragging(0))
        node->position = node->position + ImGui::GetIO().MouseDelta;

      ImU32 node_bg_color = (node_hovered_in_list == i || node_hovered_in_scene == i || (node_hovered_in_list == -1 && node_selected == i)) ? ImColor(75,75,75) : ImColor(60,60,60);
      draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 4.0f); 
      draw_list->AddRect(node_rect_min, node_rect_max, ImColor(100,100,100), 4.0f);
      ImGui::PopID();
    }
    draw_list->ChannelsMerge();



    //Draw and Update Node IO slots
    for(size_t i = 0; i < editor->nodes.count; i++){
      EditorNode *node = &editor->nodes[i];

      ImGui::PushID(i);
      ImVec2 node_rect_min = offset + node->position - ImVec2(16,16);
      ImGui::SetCursorScreenPos(node_rect_min);
      ImVec2 size = ImVec2(node->size.x + 32, node->size.x + 32);
      ImGui::InvisibleButton("node", size);

      bool hovered_slot_is_input = false;
      int hovered_slot_index = -1;

      if(ImGui::IsItemHovered()) {
        size_t total_slot_count = node->input_count + node->output_count;
        for(size_t slot_index = 0; slot_index < total_slot_count; slot_index++){
          bool is_input_slot = slot_index < node->input_count;
          const float NODE_SLOT_RADIUS_SQUARED = NODE_SLOT_RADIUS*NODE_SLOT_RADIUS;
          ImVec2 deltaSlot = is_input_slot ? node->GetInputSlotPos(slot_index) : node->GetOutputSlotPos(slot_index - node->input_count);
          deltaSlot -= canvasMouseCoords; 
          deltaSlot.x = abs(deltaSlot.x);
          deltaSlot.y = abs(deltaSlot.y);

          if((deltaSlot.x*deltaSlot.x)+(deltaSlot.y*deltaSlot.y) < NODE_SLOT_RADIUS_SQUARED){
            if(is_input_slot){
              hovered_slot_index = slot_index;
              hovered_slot_is_input = true;
            } else {
              hovered_slot_index = slot_index - node->input_count;
              hovered_slot_is_input = false;
            }
            break;
          }
        }
      }

      const ImColor default_color = ImColor(150,150,150,150);
      const ImColor hover_color   = ImColor(220, 150, 150, 150);
      for(int slot_idx = 0; slot_idx < node->input_count; slot_idx++)
        draw_list->AddCircleFilled(offset + node->GetInputSlotPos(slot_idx), NODE_SLOT_RADIUS, (hovered_slot_index == slot_idx && hovered_slot_is_input) ? hover_color : default_color);
      for(int slot_idx = 0; slot_idx < node->output_count; slot_idx++)
        draw_list->AddCircleFilled(offset + node->GetOutputSlotPos(slot_idx), NODE_SLOT_RADIUS, (hovered_slot_index == slot_idx && hovered_slot_is_input == false) ? hover_color : default_color);


      if(hovered_slot_index != -1){
        if(ImGui::IsMouseDoubleClicked(0)){
          if(hovered_slot_is_input == false){
            node->outputConnections.count = 0;
            dragNodeIndex = -1;
          }
        } else if(ImGui::IsMouseClicked(0)){
          if(dragNodeIndex == -1 && !hovered_slot_is_input){
            dragNodeIndex = i;
            dragSlotIndex = hovered_slot_index;
          } else if (dragNodeIndex != -1 && dragNodeIndex != i && hovered_slot_is_input){
            EditorNode *sourceNode = &editor->nodes[dragNodeIndex];
            NodeConnection connection = {};
            connection.node_index = i;
            connection.input_index = hovered_slot_index;
            //TORIN(Torin) Creating a output connection that goes to the same input
            // **SHOULD** be imposible but make sure thats true
            ArrayAdd(connection, sourceNode->outputConnections);
            dragNodeIndex = -1;
          }
        }
      }
      
      ImGui::PopID();
    }

    //@Dragging 
    if(dragNodeIndex != -1){
      EditorNode *sourceNode = &editor->nodes[dragNodeIndex];
      ImVec2 p1 = ImGui::GetMousePos();
      ImVec2 p2 = offset + sourceNode->GetOutputSlotPos(dragSlotIndex);
      draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
    }
    
    // Open context menu
    if (!ImGui::IsAnyItemHovered() && ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1) && dragNodeIndex == -1){
      node_selected = node_hovered_in_list = node_hovered_in_scene = -1;
      open_context_menu = true;
    }

    if(dragNodeIndex != -1 && ImGui::IsMouseClicked(1)){
      dragNodeIndex = -1;
    }

    #if 1
    if (open_context_menu){
      ImGui::OpenPopup("context_menu");
      if (node_hovered_in_list != -1)
        node_selected = node_hovered_in_list;
      if (node_hovered_in_scene != -1)
        node_selected = node_hovered_in_scene;
    }
    #endif

#if 1
    // Draw context menu
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));
    if (ImGui::BeginPopup("context_menu"))
    {
        ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
        EditorNode* node = node_selected != -1 ? &editor->nodes[node_selected] : NULL;

        if(node) {
          if(ImGui::MenuItem("Delete")) {
            ArrayRemoveAtIndexUnordered(node_selected, editor->nodes);
            node_selected = 0;
            node = 0;
          }
        }
        else
        {
          for(size_t i = 0; i < LogicNodeType_COUNT; i++){
            if(ImGui::MenuItem(LogicNodeName[i])) {
              auto node = CreateNode((LogicNodeType)i, editor);
              node->position = canvasMouseCoords;
            }
          }


            //if (ImGui::MenuItem("Add")) { nodes.push_back(Node(nodes.Size, "New node", scene_pos, 0.5f, ImColor(100,100,200), 2, 2)); }
            //if (ImGui::MenuItem("Paste", NULL, false, false)) {}
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    #endif

    // Scrolling
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
        scrolling = scrolling - ImGui::GetIO().MouseDelta;

    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndGroup();

    ImGui::End();
}


int main(){
  QuickAppStart("Hardware Simulator", 1280, 720);
  
  Editor editor = {};
  //CreateNode(LogicNodeType_AND, &editor);
  //CreateNode(LogicNodeType_OR, &editor);
  //CreateNode(LogicNodeType_XOR, &editor);

  QuickAppLoop([&]() {
    SimulationStep(&editor);
    DrawEditor(&editor);
  });
}
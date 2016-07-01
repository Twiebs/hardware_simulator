#define QUICKAPP_IMPLEMENTATION
#define QUICKAPP_IMGUI
#define QUICKAPP_RENDER
#include "QuickApp.h"

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
    if(array.data != 0) array.data = (T*)realloc(array.data, sizeof(T) * array.capacity);
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
  LogicNodeType_INPUT,
  LogicNodeType_OUTPUT,
  LogicNodeType_COUNT,
};

static const char *LogicNodeName[] = {
  "AND",
  "OR",
  "XOR",
  "INPUT",
  "OUTPUT",
};

struct NodeConnection {
  uint16_t block_index;
  uint32_t node_index;
  uint32_t io_index;
};

//NOTE(Torin) This EditorNode is used to represent nodes only within the editor
//Its implementation is simple and naive inorder to simplify the editor code as much as possible
//and allow users to modify the data it represents as easily as possible.  When the simulator is run
//editor nodes are 'compiled' into SimulatorNodes which are much more efficent but are immuatable

//TODO(Torin) Nodes should be seperated into two different types
//BasicNodes and CompoundNodes.  Basic nodes have static arrays because
//there max inputs are 2 and their max outputs are 1.  This is a more
//realisitic yet counterintuitive representation of the data because we
//can then crush all of the compound nodes into chains of BasicNodes during
//a preprocess step before the Simulation is run 

struct EditorNode {
  LogicNodeType type;
  size_t input_count;
  size_t output_count;

  ImVec2 position;
  ImVec2 size;

  uint8_t input_was_set[2];
  uint8_t input_state[2];
  //NOTE(Torin) somthing like this needs to be stored for when nodes are deleted
  DynamicArray<NodeConnection> inputConnections;
  //TODO(Torin) This should really be a DArray of DArrays for outputConections
  DynamicArray<NodeConnection> outputConnections;

  uint8_t signal_state;
  ImVec2 GetInputSlotPos(int slot_no) const   { return ImVec2(position.x, position.y + size.y * ((float)slot_no+1) / ((float)2+1)); }
  ImVec2 GetOutputSlotPos(int slot_no) const  { return ImVec2(position.x + size.x, position.y + size.y * ((float)slot_no+1) / ((float)1+1)); }
};

struct NodeBlock {
  static const size_t NODE_COUNT = 256;
  size_t currentOccupiedCount;
  uint8_t isOccupied[NODE_COUNT];
  EditorNode nodes[NODE_COUNT];
};

struct Editor {
  DynamicArray<NodeBlock *> nodeBlocks;
  DynamicArray<uint32_t> selectedNodes;
};

#define INVALID_NODE_ID UINT32_MAX

EditorNode *GetNextAvailableNode(Editor *editor){
  for(size_t blockIndex = 0; blockIndex < editor->nodeBlocks.count; blockIndex++){
    NodeBlock *block = &editor->nodeBlocks[blockIndex];
    if(block->currentOccupiedCount != NODE_COUNT){
      for(size_t nodeIndex; nodeIndex < NodeBlock::NODE_COUNT; i++){
        if(!block->isOccupied[nodeIndex]){
          return &block->nodes[nodeIndex];
        }
      }
    }
  }

  NodeMemoryBlock *block = (NodeMemoryBlock *)malloc(sizeof(NodeMemoryBlock));
  memset(block, 0, sizeof(NodeMemoryBlock));
  ArrayAdd(block, editor->nodeBlocks);
  return GetNextAvailableNode(editor);
}

EditorNode* CreateNode(LogicNodeType type, Editor *editor){
  EditorNode *node = GetNextAvailableNode(editor);
  node->type = type;

  switch(node->type){
    case LogicNodeType_INPUT:{
      node->input_count = 0;
      node->output_count = 1;
    } break;

    case LogicNodeType_OUTPUT: {
      node->input_count = 1;
      node->output_count = 0;
    }break;

    default: {
      node->input_count = 2;
      node->output_count = 1;
    } break;
  }

  return node;
}

EditorNode *GetNode(uint32_t block_index, uint32_t node_index, Editor *editor){
  assert(block_index < editor->nodeBlocks.count);
  NodeBlock *block = &editor->nodeBlocks[block_index];
  assert(node_index < NodeBlock::NODE_COUNT);
  EditorNode *result = &block->nodes[node_index];
  return result;
}

EditorNode *GetNode(const NodeConnection& connection, Editor *editor){
  return GetNode(connection.block_index, connection.node_index, editor);
}

static inline void SimulateNode(EditorNode *node, Editor *editor);

static inline
void TransmitOutputStateAndSimulateConnectedNodes(EditorNode *node, uint8_t outputState, Editor *editor){
  for(size_t i = 0; i < node->outputConnections.count; i++){
    auto &connection = node->outputConnections[i];
    EditorNode *connectedNode = &editor->nodes[connection.node_index];
    connectedNode->input_state[connection.slot_index] = outputState;
    connectedNode->input_was_set[connection.slot_index] = 1;
    SimulateNode(connectedNode, editor);
  }
}

static inline
void SimulateNode(EditorNode *node, Editor *editor){
  switch(node->type){
    case LogicNodeType_INPUT: {
      for(size_t connectionIndex = 0; connectionIndex < node->outputConnections.count; connectionIndex++){
        auto& connection = node->outputConnections[connectionIndex];
        EditorNode *connectedNode = GetNode(connection, editor);
        connectedNode->input_state[connection.slot_index] = node->signal_state;
        connectedNode->input_was_set[connection.slot_index] = 1;
      }
    }break;

    case LogicNodeType_OUTPUT: {
      if(node->input_was_set[0] == 1){
        node->signal_state = node->input_state[0];
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
  }
}

//TODO(Torin) Why should this care about an editor ptr
//Just directly pass the node block array it signals intent better
static inline
void SimulationStep(Editor *editor){
  for(size_t blockIndex = 0; blockIndex < editor->nodeBlocks.count; blockIndex++){
    NodeBlock *block = &editor->nodeBlocks[blockIndex];
    for(size_t nodeIndex = 0; nodeIndex < NodeBlock::NODE_COUNT; nodeIndex++){
      if(block->isOccupied[nodeIndex]){
        SimulateNode(&block->nodes[nodeIndex]);
      }
    }
  }
}

static inline
void DrawEditorNode(EditorNode *node){
  switch(node->type){
    case LogicNodeType_INPUT:{
      const char *text = node->signal_state ? "1" : "0";
      if(ImGui::Button(text, ImVec2(32, 32))){
        node->signal_state = !node->signal_state;
      }
    }break;

    case LogicNodeType_OUTPUT:{
      const char *text = node->signal_state ? "1" : "0";
      ImGui::Text(text);
    }break;

    default:{
      ImGui::Text(LogicNodeName[node->type]);
    } break;
  }
}

static inline
void iterate_nodes(NodeBlock *blocks, size_t count, std::function<void(EditorNode*, size_t)> procedure){
  for(size_t i = 0; i < count; i++){ 
    NodeBlock *block = &blockarray[i]; 
    for(size_t n = 0; n < NodeBlock::NODE_COUNT; n++){ 
      if(block->isOccupied[n]) {
        procedure(&block->nodes[n], n);
      }
    }
  }
}



void DrawEditor(Editor *editor){
  static const float NODE_SLOT_RADIUS = 6.0f;
  static const ImVec2 NODE_WINDOW_PADDING(16.0f, 16.0f);
  static const ImU32 GRID_COLOR = ImColor(200,200,200,40);
  static const float GRID_SIZE = 32.0f;

  static ImVec2 scrolling = ImVec2(0.0f, 0.0f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
  ImGui::SetNextWindowSize(ImVec2(1280, 720));
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  if(!ImGui::Begin("Example: Custom Node Graph", 0, flags)){
    ImGui::End();
    return;
  }

  // Draw a list of nodes on the left side
  bool open_context_menu = false;
  
  static bool is_context_menu_open = false;
  static int node_hovered = -1;
  static int node_selected = -1;
  static int dragNodeIndex = -1;
  static int dragSlotIndex = -1;

  if(is_context_menu_open == false){
    node_hovered = -1;
  }


  ImGui::BeginGroup();

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

  //@Draw @Grid
  ImVec2 win_pos = ImGui::GetCursorScreenPos();
  ImVec2 canvas_sz = ImGui::GetWindowSize();
  for(float x = fmodf(offset.x,GRID_SIZE); x < canvas_sz.x; x += GRID_SIZE)
    draw_list->AddLine(ImVec2(x,0.0f)+win_pos, ImVec2(x,canvas_sz.y)+win_pos, GRID_COLOR);
  for(float y = fmodf(offset.y,GRID_SIZE); y < canvas_sz.y; y += GRID_SIZE)
    draw_list->AddLine(ImVec2(0.0f,y)+win_pos, ImVec2(canvas_sz.x,y)+win_pos, GRID_COLOR);

  // Display links
  draw_list->ChannelsSetCurrent(0); // Background

  iterate_nodes(editor->nodeBlocks.data, editor->nodeBlocks.count, [](EditorNode *a, size_t i){
    for(size_t n = 0; n < a->outputConnections.count; n++){
      ImVec2 p1 = offset + a->GetOutputSlotPos(0);
      EditorNode *b = &editor->nodes[a->outputConnections[n].node_index];
      ImVec2 p2 = offset + b->GetInputSlotPos(a->outputConnections[n].slot_index);
      draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
    }
  });

  iterate_nodes(editor->nodeBlocks.data, editor->nodeBlocks.count, [](EditorNode *node, size_t i){
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
      node_hovered = i;
      if(ImGui::IsMouseClicked(1)) {
        open_context_menu = 1;
      }
    }

    bool node_moving_active = ImGui::IsItemActive();
    if(node_moving_active)
      node_selected = i;
    if(node_moving_active && ImGui::IsMouseDragging(0))
      node->position = node->position + ImGui::GetIO().MouseDelta;

    ImU32 node_bg_color = (node_hovered == i || node_selected == i) ? ImColor(75,75,75) : ImColor(60,60,60);
    draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 4.0f); 
    draw_list->AddRect(node_rect_min, node_rect_max, ImColor(100,100,100), 4.0f);
    ImGui::PopID();
  });
  draw_list->ChannelsMerge();



  //Draw and Update Node IO slots
  for(size_t i = 0; i < editor->nodes.count; i++){
    EditorNode *node = &editor->nodes[i];

    ImVec2 node_rect_min = offset + node->position - ImVec2(12,12);
    ImGui::SetCursorScreenPos(node_rect_min);
    ImVec2 size = ImVec2(node->size.x + 32, node->size.x + 32);


    ImGui::PushID(i);
    ImGui::InvisibleButton("##node", size);

    bool hovered_slot_is_input = false;
    int hovered_slot_index = -1;


    //TODO(Torin) HACK imgui is not returning true on any of these when it should so were checking every single
    //node and its io slots for intersection on a mouse click
    if(ImGui::IsItemHovered() || ImGui::IsItemClicked() || ImGui::IsItemActive() || ImGui::IsMouseClicked(0)) {
      size_t total_slot_count = node->input_count + node->output_count;
      for(size_t slot_index = 0; slot_index < total_slot_count; slot_index++){
        bool is_input_slot = slot_index < node->input_count;
        const float NODE_SLOT_RADIUS_SQUARED = NODE_SLOT_RADIUS*NODE_SLOT_RADIUS;
        ImVec2 deltaSlot = is_input_slot ? node->GetInputSlotPos(slot_index) : node->GetOutputSlotPos(slot_index - node->input_count);
        deltaSlot -= scrolling;
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

    static bool do_test = false;
    if(ImGui::IsMouseClicked(0)){
      do_test = true;
    }

    static const ImColor default_color = ImColor(150,150,150,150);
    static const ImColor hover_color   = ImColor(220, 150, 150, 150);
    for(int slot_idx = 0; slot_idx < node->input_count; slot_idx++) {
      const ImColor color = ((hovered_slot_index == slot_idx) && (hovered_slot_is_input == true)) ? hover_color : default_color;
      draw_list->AddCircleFilled(offset + node->GetInputSlotPos(slot_idx), NODE_SLOT_RADIUS, color);
    }

    for(int slot_idx = 0; slot_idx < node->output_count; slot_idx++){
      const ImColor color = ((hovered_slot_index == slot_idx) && (hovered_slot_is_input == false)) ? hover_color : default_color;
      draw_list->AddCircleFilled(offset + node->GetOutputSlotPos(slot_idx), NODE_SLOT_RADIUS, color);
    }
      

    if(hovered_slot_index != -1){
      if(ImGui::IsMouseDoubleClicked(0)){
        if(hovered_slot_is_input == false){
          for(size_t i = 0; i < node->outputConnections.count; i++){
            NodeConnection *connection = &node->outputConnections[i];
            EditorNode *destNode = &editor->nodes[connection->node_index];
            ArrayRemoveAtIndexUnordered(connection->slot_index, destNode->inputConnections);
          }
          node->outputConnections.count = 0;
          dragNodeIndex = -1;
        }
      } else if(ImGui::IsMouseClicked(0)){
        if(dragNodeIndex == -1 && !hovered_slot_is_input){
          dragNodeIndex = i;
          dragSlotIndex = hovered_slot_index;
        } else if (dragNodeIndex != -1 && dragNodeIndex != i && hovered_slot_is_input){
          EditorNode *sourceNode = &editor->nodes[dragNodeIndex];
          EditorNode *destNode = node;

          //TODO(Torin) Creating a output connection that goes to the same input
          // **SHOULD** be imposible but make sure thats true
          //TODO(Torin) ^^^^^ ASSUMTION IS WRONG WRONG WRONG FIX FIX FIX

          NodeConnection outputToInput = {};
          outputToInput.node_index = i;
          outputToInput.slot_index = hovered_slot_index;
          ArrayAdd(outputToInput, sourceNode->outputConnections);

          NodeConnection inputToOutput;
          inputToOutput.node_index = dragNodeIndex;
          inputToOutput.slot_index = 0; //TODO(Torin) Only one output currently supported
          ArrayAdd(inputToOutput, destNode->inputConnections);

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

  if(ImGui::IsMouseClicked(1) && ImGui::IsMouseDown(1)){
    if(dragNodeIndex != -1){
      dragNodeIndex = -1;
    } else {
      open_context_menu = 1;
    }
  }

  if(open_context_menu){
    ImGui::OpenPopup("context_menu");
    is_context_menu_open = true;
  }

  // Draw context menu
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));
  if(ImGui::BeginPopup("context_menu")){
    ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
    if(node_hovered != -1){
      EditorNode *node = &editor->nodes[node_hovered];
      if(ImGui::MenuItem("Delete")){
        //TODO(Torin) Refactor!!! inputConnections does not need to be dynamic for basic node types
        assert(node->inputConnections.count <= node->input_count);
        for(size_t i = 0; i < node->inputConnections.count; i++){
          NodeConnection *connection = &node->inputConnections[i];
          EditorNode *inputSource = &editor->nodes[connection->node_index];
          ArrayRemoveAtIndexUnordered(connection->slot_index, inputSource->outputConnections);
        }

        for(size_t i = 0; i < node->outputConnections.count; i++){
          NodeConnection *connection = &node->outputConnections[i];
          EditorNode *connectionDest = &editor->nodes[connection->node_index];
          ArrayRemoveAtIndexUnordered(connection->slot_index, connectionDest->inputConnections);
        }

        ArrayRemoveAtIndexUnordered(node_hovered, editor->nodes);
        node_selected = 0;
        node = 0;
      }
    }
    else {
      for(size_t i = 0; i < LogicNodeType_COUNT; i++){
        if(ImGui::MenuItem(LogicNodeName[i])) {
          auto node = CreateNode((LogicNodeType)i, editor);
          node->position = canvasMouseCoords;
        }
      }
    }
    ImGui::EndPopup();
  } else {
    is_context_menu_open = false;
  }
  ImGui::PopStyleVar();

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

  QuickAppLoop([&]() {
    SimulationStep(&editor);
    DrawEditor(&editor);
  });
}
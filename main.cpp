#define QUICKAPP_IMPLEMENTATION
#define QUICKAPP_IMGUI
#define QUICKAPP_RENDER
#include "QuickApp.h"

#pragma clang diagnostic ignored "-Wformat-security"

template<typename T>
struct DynamicArray {
  size_t capacity;
  size_t count;
  T *data;

  DynamicArray() {
    capacity = 0;
    count = 0;
    data = 0;
  }

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

template<typename T>
void ArrayRemoveValueUnordered(const T& t, DynamicArray<T>& array){
  for(size_t i = 0; i < array->count; i++){
    if(array->data[i] == t) {
      array->data[i] = array->data[array->count - 1];
      array->count -= 1;
      return;
    }
    assert(false);
  }
}

template <typename T>
int ArrayContains(const T& value, DynamicArray<T>& array){
  for(size_t i = 0; i < array.count; i++){
    if(array.data[i] == value){
      return 1;
    }
  }
  return 0;
}

template<typename T>
void ArrayDestroy(DynamicArray<T>& array){
  if(array.data != 0) free(array.data);
  array.count = 0;
  array.capacity = 0;
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

struct NodeIndex {
  union { 
    struct {
      uint16_t block_index;
      uint16_t node_index;
    };
    uint32_t combined;
  };
};

struct NodeConnection {
  NodeIndex node_index;
  uint32_t io_index;
};

struct ICNodeConnection {
  uint32_t node_offset;
  uint32_t io_index;
};

struct ICNode {
  uint32_t type;
  uint32_t input_count;
  uint32_t output_count;
  //uint32_t connection_count_per_output[output_count];
  //uint32_t output_connections[sum(connection_count_per_output)]
};

struct ICDefinition {
  uint32_t input_count;
  uint32_t output_count;
};

enum NodeState : uint8_t {
  NodeState_LOW,
  NodeState_HIGH,
  NodeState_NONE,
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
  uint32_t type;
  size_t input_count;
  size_t output_count;
  ImVec2 position;
  ImVec2 size;

  NodeState *input_state;                           //NodeState[input_count]
  NodeConnection *inputConnections;                 //NodeConnection[input_count]
  DynamicArray<NodeConnection> *output_connections; //DynamicArray<NodeConnection>[output_count]

  NodeState signal_state;
};

EditorNode *CreateNode(uint32_t input_count, uint32_t output_count) {
  size_t required_memory = sizeof(EditorNode);
  required_memory = (required_memory + 0x7) & ~0x7;
  required_memory += sizeof(NodeState) * input_count;
  required_memory = (required_memory + 0x3) & ~0x3;
  required_memory += sizeof(NodeConnection) * input_count;
  required_memory = (required_memory + 0x3) & ~0x3;
  required_memory += sizeof(DynamicArray<NodeConnection>) * output_count;

  EditorNode *node = (EditorNode *)malloc(required_memory);
  memset(node, 0, required_memory);
  node->input_count = input_count;
  node->output_count = output_count;

  uint8_t *current = (uint8_t *)(node + 1);
  current = (current + 0x7) & ~0x7;
  node->input_state = (NodeState *)current;
  current += sizeof(NodeState) * input_count;
  current = (current + 0x3) & ~0x3;
  node->inputConnections = (NodeConnection *)current;
  current += sizeof(NodeConnection) * input_count;
  current = (current + 0x3) & ~0x3;
  node->output_connections = current;

  return node;
}


struct NodeBlock {
  static const size_t NODE_COUNT = 256;
  size_t currentOccupiedCount;
  uint8_t isOccupied[NODE_COUNT];
  EditorNode nodes[NODE_COUNT];
};

enum EditorMode {
  EditorMode_None,
  EditorMode_SelectBox,
};

struct Editor {
  DynamicArray<NodeBlock *> nodeBlocks;
  DynamicArray<NodeIndex> selectedNodes;
  DynamicArray<ICDefinition *> icdefs;
  
  EditorMode mode;
  union {
    ImVec2 selectBoxOrigin;
  };
};

struct Rectangle {
  float minX, minY;
  float maxX, maxY;
};

int Intersects(const Rectangle& a, const Rectangle& b){
  if(a.maxX < b.minX || a.minX > b.maxX) return 0;
  if(a.maxY < b.minY || a.minY > b.maxY) return 0;
  return 1;
}

int Max(int a, int b){
  int result = a > b ? a : b;
  return result;
}

int Min(int a, int b){
  int result = a < b ? a : b;
  return result;
}

static inline
bool IsValid(NodeIndex index){
  bool result = (index.node_index != UINT16_MAX && index.block_index != UINT16_MAX); 
  return result;
}

static inline
NodeIndex InvalidNodeIndex(){
  NodeIndex result;
  result.block_index = UINT16_MAX;
  result.node_index = UINT16_MAX;
  return result;
}

static inline 
bool operator==(const NodeIndex& a, const NodeIndex& b){
  if(a.block_index != b.block_index) return 0;
  if(a.node_index != b.node_index) return 0;
  return 1;
}

static inline 
bool operator!=(const NodeIndex& a, const NodeIndex& b){
  if(a.block_index == b.block_index && a.node_index == b.node_index) return 0;
  return 1;
}

static inline
EditorNode *GetNextAvailableNode(Editor *editor){
  for(size_t blockIndex = 0; blockIndex < editor->nodeBlocks.count; blockIndex++){
    NodeBlock *block = editor->nodeBlocks[blockIndex];
    if(block->currentOccupiedCount != NodeBlock::NODE_COUNT){
      for(size_t nodeIndex; nodeIndex < NodeBlock::NODE_COUNT; nodeIndex++){
        if(!block->isOccupied[nodeIndex]){
          block->isOccupied[nodeIndex] = 1;
          block->currentOccupiedCount++;
          return &block->nodes[nodeIndex];
        }
      }
    }
  }

  NodeBlock*block = (NodeBlock *)malloc(sizeof(NodeBlock));
  memset(block, 0, sizeof(NodeBlock));
  ArrayAdd(block, editor->nodeBlocks);
  return GetNextAvailableNode(editor);
}

EditorNode* CreateNode(uint32_t type, Editor *editor){
  EditorNode *node = GetNextAvailableNode(editor);
  node->inputConnections[0].node_index = InvalidNodeIndex();
  node->inputConnections[1].node_index = InvalidNodeIndex();
  node->type = type;
  node->outputConnections = (DynamicArray<NodeConnection> *)malloc(sizeof(DynamicArray<NodeConnection))

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

  node->size = ImVec2(64, 64);
  return node;
}

static inline
EditorNode *GetNode(NodeIndex index, Editor *editor){
  assert(index.block_index < editor->nodeBlocks.count);
  NodeBlock *block = editor->nodeBlocks[index.block_index];
  assert(index.node_index < NodeBlock::NODE_COUNT);
  EditorNode *result = &block->nodes[index.node_index];
  return result;
}

static inline
void DeleteNode(NodeIndex index, Editor *editor){
  assert(index.block_index < editor->nodeBlocks.count);
  assert(index.node_index < NodeBlock::NODE_COUNT);
  EditorNode *node = GetNode(index, editor);

  for(size_t i = 0; i < node->input_count; i++){
    NodeConnection *connection = &node->inputConnections[i];
    if(IsValid(connection->node_index) == false) continue;
    EditorNode *inputSource = GetNode(connection->node_index, editor);
    bool wasConnectionRemoved = false;

    DynamicArray<NodeConnection> &sourceConnections = inputSource->outputConnections[connection->io_index];
    for(size_t n = 0; n < sourceConnections.count; n++){
      const NodeConnection *sourceConnection = &sourceConnections[n];
      if((sourceConnection->node_index == index) && sourceConnection->io_index == i){
        ArrayRemoveAtIndexUnordered(n, sourceConnections);
        wasConnectionRemoved = true;              
      }
    }
    assert(wasConnectionRemoved && "inputConnection did not match any outputConnection");
  }

  for(size_t i = 0; i < node->outputConnections.count; i++){
    NodeConnection *connection = &node->outputConnections[i];
    EditorNode *connectionDest = GetNode(connection->node_index, editor);
    connectionDest->inputConnections[connection->io_index].node_index = InvalidNodeIndex();
  }

  NodeBlock *block = editor->nodeBlocks[index.block_index];
  block->isOccupied[index.node_index] = 0;
  ArrayDestroy(block->nodes[index.node_index].outputConnections);
  memset(&block->nodes[index.node_index], 0, sizeof(EditorNode));
  block->currentOccupiedCount -= 1;
}

static inline void SimulateNode(EditorNode *node, Editor *editor);

static inline
void TransmitOutputAndSimulateConnectedNodes(EditorNode *node, uint8_t outputState, Editor *editor){
  assert(outputState != NodeState_NONE);
  node->signal_state = (NodeState)outputState;
  for(size_t i = 0; i < node->outputConnections.count; i++){
    NodeConnection *connection = &node->outputConnections[i];
    EditorNode *connectedNode = GetNode(connection->node_index, editor);
    connectedNode->input_state[connection->io_index] = (NodeState)outputState;
    SimulateNode(connectedNode, editor);
  }
}

static inline
void SimulateNode(EditorNode *node, Editor *editor){
  switch(node->type){
    case LogicNodeType_INPUT: {
      for(size_t connectionIndex = 0; connectionIndex < node->outputConnections.count; connectionIndex++){
        NodeConnection *connection = &node->outputConnections[connectionIndex];
        EditorNode *connectedNode = GetNode(connection->node_index, editor);
        connectedNode->input_state[connection->io_index] = node->signal_state;
      }
    }break;

    case LogicNodeType_OUTPUT: {
      if(node->input_state[0] == NodeState_NONE){
        node->signal_state = NodeState_LOW;
      } else {
        node->signal_state = node->input_state[0];
      }
    }break;

    case LogicNodeType_AND:{
      if(node->input_state[0] == NodeState_NONE) return;
      if(node->input_state[0] == NodeState_NONE) return;
      uint8_t output_signal = node->input_state[0] && node->input_state[1];
      TransmitOutputAndSimulateConnectedNodes(node, output_signal, editor);
    }break;

    case LogicNodeType_OR:{
      if(node->input_state[0] == NodeState_NONE) return;
      if(node->input_state[1] == NodeState_NONE) return;
      uint8_t output_signal = node->input_state[0] || node->input_state[1];
      TransmitOutputAndSimulateConnectedNodes(node, output_signal, editor);
    }break;

    case LogicNodeType_XOR:{
      if(node->input_state[0] == NodeState_NONE) return;
      if(node->input_state[1] == NodeState_NONE) return;
      uint8_t output_signal = node->input_state[0] || node->input_state[1];
      output_signal = output_signal && !(node->input_state[0] && node->input_state[1]);
      TransmitOutputAndSimulateConnectedNodes(node, output_signal, editor);
    }break;

    default:{


    }break;
  }
}



static inline
void iterate_nodes(NodeBlock **blocks, size_t count, std::function<void(EditorNode*, NodeIndex)> procedure){
  for(size_t i = 0; i < count; i++){ 
    NodeBlock *block = blocks[i]; 
    for(size_t n = 0; n < NodeBlock::NODE_COUNT; n++){ 
      if(block->isOccupied[n]) {
        NodeIndex index = { (uint16_t)i, (uint16_t)n };
        procedure(&block->nodes[n], index);
      }
    }
  }
}

static inline
void iterate_nodes(Editor *editor, std::function<void(EditorNode*)> procedure){
  NodeBlock **blocks = editor->nodeBlocks.data;
  size_t count = editor->nodeBlocks.count;
  for(size_t i = 0; i < count; i++){ 
    NodeBlock *block = blocks[i]; 
    for(size_t n = 0; n < NodeBlock::NODE_COUNT; n++){ 
      if(block->isOccupied[n]) {
        procedure(&block->nodes[n]);
      }
    }
  }
}

//TODO(Torin) Why should this care about an editor ptr
//Just directly pass the node block array it signals intent better

//TODO(Torin) With this model nodes are being simulated more than once
static inline
void SimulationStep(Editor *editor){
  iterate_nodes(editor, [](EditorNode *node){
    memset(node->input_state, NodeState_NONE, node->input_count * sizeof(NodeState));
  });

  for(size_t blockIndex = 0; blockIndex < editor->nodeBlocks.count; blockIndex++){
    NodeBlock *block = editor->nodeBlocks[blockIndex];
    for(size_t nodeIndex = 0; nodeIndex < NodeBlock::NODE_COUNT; nodeIndex++){
      if(block->isOccupied[nodeIndex]){
        SimulateNode(&block->nodes[nodeIndex], editor);
      }
    }
  }
}

void DrawEditor(Editor *editor){
  static const ImU32 GRID_COLOR = ImColor(200,200,200,40);
  static const float GRID_SIZE = 16.0f;
  static const float NODE_SLOT_RADIUS = 6.0f;
  static const ImVec2 NODE_WINDOW_PADDING(8.0f, 8.0f);

  static const ImColor CONNECTION_DEFAULT_COLOR = ImColor(150,50,50);
  static const ImColor CONNECTION_ACTIVE_COLOR = ImColor(220, 220, 110);

  static const ImColor NODE_BACKGROUND_SELECTED_COLOR = ImColor(90,90,90);
  static const ImColor NODE_BACKGROUND_DEFAULT_COLOR = ImColor(60,60,60);
  static const ImColor NODE_OUTLINE_COLOR = ImColor(100,100,100);

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
  static NodeIndex node_hovered = InvalidNodeIndex();
  static NodeIndex dragNodeIndex = InvalidNodeIndex();

  static int dragSlotIndex = -1;

  if(is_context_menu_open == false){
    node_hovered = InvalidNodeIndex();
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


  auto GetNodeInputSlotPos = [](const EditorNode *node, const int slotIndex) -> ImVec2 {
    auto result = ImVec2(position.x, position.y + size.y * ((float)slot_no+1) / ((float)2+1));
    return result;
  };

  auto GetNodeOutputSlotPos = [](const EditorNode *node, const int slotIndex) -> ImVec2 {
    auto result = ImVec2(position.x + size.x, position.y + size.y * ((float)slot_no+1) / ((float)1+1));
    return result;
  };



  iterate_nodes(editor->nodeBlocks.data, editor->nodeBlocks.count, [&](EditorNode *a, NodeIndex index){
    for(size_t n = 0; n < a->outputConnections.count; n++){
      ImVec2 p1 = offset + a->GetOutputSlotPos(0);
      EditorNode *b = GetNode(a->outputConnections[n].node_index, editor);
      ImVec2 p2 = offset + GetInputSlotPos(b, a->outputConnections[n].io_index);
      auto color = a->signal_state ? CONNECTION_ACTIVE_COLOR : CONNECTION_DEFAULT_COLOR;
      draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, color, 3.0f);
    }
  });

  bool nodeWasClicked = false;
  iterate_nodes(editor->nodeBlocks.data, editor->nodeBlocks.count, [&](EditorNode *node, NodeIndex index){
    ImVec2 node_rect_min = offset + node->position;
    ImVec2 node_rect_max = node_rect_min + node->size;

    ImGui::PushID(index.combined);

    draw_list->ChannelsSetCurrent(1); // Foreground
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(node_rect_min + ImVec2(24, 24));
    switch(node->type){
      case LogicNodeType_INPUT:{
        ImGui::SetCursorScreenPos(node_rect_min + ImVec2(4, 4));
        const char *text = node->signal_state ? "1" : "0";
        auto color = node->signal_state ? CONNECTION_ACTIVE_COLOR : CONNECTION_DEFAULT_COLOR;
        //ImGui::PushStyleColor(ImGuiCol_Button, color);

        if(ImGui::Button(text, ImVec2(32, 32))){
          if(node->signal_state == NodeState_LOW){
            node->signal_state = NodeState_HIGH;
          } else if (node->signal_state == NodeState_HIGH){
            node->signal_state = NodeState_LOW;
          } else {
            assert(false);
          }
        }
        //ImGui::PopStyleColor();
      }break;

      case LogicNodeType_OUTPUT:{
        const char *text = node->signal_state ? "1" : "0";
        ImGui::Text(text);
      }break;

      default:{
        ImGui::Text(LogicNodeName[node->type]);
      } break;
    }
    ImGui::EndGroup();

    // Display node box
    ImGui::SetCursorScreenPos(node_rect_min);

    ImGui::InvisibleButton("node", node->size);
    ImGui::PopID();
    
    if(ImGui::IsItemHovered()){
      node_hovered = index;
      if(ImGui::IsMouseClicked(1)) {
        open_context_menu = 1;
      } else if (ImGui::IsMouseClicked(0) && editor->mode != EditorMode_SelectBox){
        if(!ArrayContains(node_hovered, editor->selectedNodes)){
          editor->selectedNodes.count = 0;
          ArrayAdd(node_hovered, editor->selectedNodes);
        }
      }
    }

    bool node_moving_active = ImGui::IsItemActive();

    if(node_moving_active && ImGui::IsMouseDragging(0)){
      for(size_t i = 0; i < editor->selectedNodes.count; i++){
        EditorNode *selectedNode = GetNode(editor->selectedNodes[i], editor);
        selectedNode->position += ImGui::GetIO().MouseDelta;
      }
    }

    ImColor nodeColor = NODE_BACKGROUND_DEFAULT_COLOR;
    if(ArrayContains(index, editor->selectedNodes)){
      nodeColor = NODE_BACKGROUND_SELECTED_COLOR;
    }

    draw_list->ChannelsSetCurrent(0); // Background
    draw_list->AddRectFilled(node_rect_min, node_rect_max, nodeColor, 4.0f); 
    draw_list->AddRect(node_rect_min, node_rect_max, NODE_OUTLINE_COLOR, 4.0f);
  });
  draw_list->ChannelsMerge();

  //Draw and Update Node IO slots
  iterate_nodes(editor->nodeBlocks.data, editor->nodeBlocks.count, [&](EditorNode *node, NodeIndex index){
    ImVec2 node_rect_min = offset + node->position - ImVec2(12,12);
    ImGui::SetCursorScreenPos(node_rect_min);
    ImVec2 size = ImVec2(node->size.x + 32, node->size.x + 32);
    ImGui::PushID(index.combined + editor->nodeBlocks.count * 1024);
    ImGui::InvisibleButton("##node", size);
    ImGui::PopID();

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
            EditorNode *destNode = GetNode(connection->node_index, editor);
            destNode->inputConnections[connection->io_index].node_index = InvalidNodeIndex();
          }

          node->outputConnections.count = 0;
          dragNodeIndex = InvalidNodeIndex();
        }
      } else if(ImGui::IsMouseClicked(0)){
        if(!IsValid(dragNodeIndex) && !hovered_slot_is_input){
          dragNodeIndex = index;
          dragSlotIndex = hovered_slot_index;
        } else if (IsValid(dragNodeIndex) && dragNodeIndex != index && hovered_slot_is_input){
          EditorNode *sourceNode = GetNode(dragNodeIndex, editor);
          EditorNode *destNode = node;

          if(!IsValid(destNode->inputConnections[hovered_slot_index].node_index)){
            //TODO(Torin) Insure that an output connection cannot have two connections to the same input
            //It appears that this is currently imposible already **BUT** only because single input sources
            //are allowed

            NodeConnection outputToInput = {};
            outputToInput.node_index = index;
            outputToInput.io_index = hovered_slot_index;
            ArrayAdd(outputToInput, sourceNode->outputConnections);

            NodeConnection inputToOutput;
            inputToOutput.node_index = dragNodeIndex;
            inputToOutput.io_index = 0; //TODO(Torin) Only one output currently supported
            destNode->inputConnections[hovered_slot_index] = inputToOutput;

            dragNodeIndex = InvalidNodeIndex();
          }
        }
      }
    }
  });

  //@Dragging 
  if(IsValid(dragNodeIndex)){
    EditorNode *sourceNode = GetNode(dragNodeIndex, editor);
    ImVec2 p1 = ImGui::GetMousePos();
    ImVec2 p2 = offset + sourceNode->GetOutputSlotPos(dragSlotIndex);
    draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
  }

  if(ImGui::IsMouseClicked(1) && ImGui::IsMouseDown(1)){
    if(IsValid(dragNodeIndex)){
      dragNodeIndex = InvalidNodeIndex();
    } else {
      open_context_menu = 1;
    }
  }

  if(open_context_menu){
    ImGui::OpenPopup("context_menu");
    is_context_menu_open = true;
  }

  switch(editor->mode){
    case EditorMode_None:{
      if(ImGui::IsMouseDragging(0)){
        if(node_hovered != InvalidNodeIndex()) break;
        editor->selectBoxOrigin = ImGui::GetMousePos();
        editor->mode = EditorMode_SelectBox;
      }
    } break;

    case EditorMode_SelectBox:{
      if(ImGui::IsMouseDragging(0) == 0){
        editor->selectedNodes.count = 0;
        auto end = ImGui::GetMousePos();
        Rectangle selectBoxBounds;
        selectBoxBounds.minX = Min(editor->selectBoxOrigin.x, end.x);
        selectBoxBounds.maxX = Max(editor->selectBoxOrigin.x, end.x);
        selectBoxBounds.minY = Min(editor->selectBoxOrigin.y, end.y);
        selectBoxBounds.maxY = Max(editor->selectBoxOrigin.y, end.y);
        iterate_nodes(editor->nodeBlocks.data, editor->nodeBlocks.count, [&](EditorNode *node, NodeIndex index){
          Rectangle nodeRect = {node->position.x, node->position.y, node->position.x + node->size.x, node->position.y + node->size.y};
          if(Intersects(selectBoxBounds, nodeRect)){
            ArrayAdd(index, editor->selectedNodes);
          }
        });
        editor->mode = EditorMode_None;
      }
    } break;
  }

  if(editor->mode == EditorMode_SelectBox){
    ImColor color = ImColor(100, 100, 100, 100);
    draw_list->AddRectFilled(editor->selectBoxOrigin, ImGui::GetMousePos(), color);
  }

  // Draw context menu
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));
  if(ImGui::BeginPopup("context_menu")){
    ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
    if(IsValid(node_hovered)){

      if(ImGui::MenuItem("Delete")){
        if(ArrayContains(node_hovered, editor->selectedNodes)){
          for(size_t i = 0; i < editor->selectedNodes.count; i++)
            DeleteNode(editor->selectedNodes[i], editor);
          editor->selectedNodes.count = 0;
        } else {
          DeleteNode(node_hovered, editor);
        }
      }

      if(ImGui::MenuItem("Create IC")){
        DynamicArray<EditorNode *> inputs;
        DynamicArray<EditorNode *> outputs;
        DynamicArray<EditorNode *> logic;

        for(size_t i = 0; i < editor->selectedNodes.count; i++){
          EditorNode *node = GetNode(editor->selectedNodes[i], editor);
          if(node->type == LogicNodeType_INPUT) {
            ArrayAdd(node, inputs);
          } else if (node->type == LogicNodeType_OUTPUT){
            ArrayAdd(node, outputs);
          } else {
            ArrayAdd(node, logic);
          }
        }

        size_t required_memory = sizeof(ICDefinition);
        for(size_t i = 0; i < editor->selectedNodes.count; i++){
          EditorNode *node = GetNode(editor->selectedNodes[i], editor);
          required_memory += sizeof(ICNode);
          required_memory += node->output_count * sizeof(ICNodeConnection);
        }


        for(size_t i = 0; i < editor->selectedNodes.count; i++){
          DeleteNode(editor->selectedNodes[i], editor);
        }

        uint32_t node_type = LogicNodeType_COUNT + 1 + editor->icdefs.count;
        auto node = CreateNode(node_type, editor);
        node->position = offset + ImGui::GetMousePos();
        node->input_count = inputs.count;
        node->output_count = outputs.count;


        

        ArrayDestroy(inputs);
        ArrayDestroy(outputs);
        ArrayDestroy(logic);
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
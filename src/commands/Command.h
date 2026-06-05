#pragma once

#include <deque>
#include <memory>

// Mutation sites to be wrapped in Phase 4:
// - AddComponent (main.cpp line ~594)
// - DeleteComponent (main.cpp line ~none currently present)
// - MoveComponent (main.cpp line ~931)
// - Connect (main.cpp line ~618, ~627, ~698, ~788)
// - EditProperty (main.cpp line ~907, ~922, ~926, ~934)

/// Abstract base for all reversible operations.
/// Every action that mutates the circuit must implement execute() and undo()
/// so CommandHistory can apply it, reverse it, and later replay it for redo.
class Command {
public:
    /// Applies the operation to the model so the command has its intended effect.
    virtual void execute() = 0;

    /// Reverses execute() so the model returns to the state it had before the command.
    virtual void undo() = 0;

    /// Allows derived commands to clean up safely when stored through base pointers.
    virtual ~Command() = default;
};

/// Stores a bounded history of commands for undo/redo.
/// Commands live here after execution so the app can walk backward and forward
/// through user edits without each UI control needing to know about history.
class CommandHistory {
public:
    /// Names the maximum undo depth so history capacity is easy to tune without
    /// hunting for unexplained numeric limits in the implementation.
    static constexpr int MAX_DEPTH = 100;

    /// Executes a new command, stores it for future undo, and clears redo history
    /// because a new edit starts a new timeline from the current circuit state.
    void push(std::unique_ptr<Command> cmd)
    {
        if (!cmd) {
            return;
        }

        cmd->execute();
        undoStack.push_back(std::move(cmd));
        redoStack.clear();

        while (static_cast<int>(undoStack.size()) > MAX_DEPTH) {
            undoStack.pop_front();
        }
    }

    /// Reverses the most recent command and moves it to the redo stack so the
    /// user can restore that same edit later if undo was accidental.
    void undo()
    {
        if (undoStack.empty()) {
            return;
        }

        std::unique_ptr<Command> cmd = std::move(undoStack.back());
        undoStack.pop_back();
        cmd->undo();
        redoStack.push_back(std::move(cmd));
    }

    /// Re-executes the most recently undone command and returns it to undo
    /// history, recreating the same model change that undo just removed.
    void redo()
    {
        if (redoStack.empty()) {
            return;
        }

        std::unique_ptr<Command> cmd = std::move(redoStack.back());
        redoStack.pop_back();
        cmd->execute();
        undoStack.push_back(std::move(cmd));

        while (static_cast<int>(undoStack.size()) > MAX_DEPTH) {
            undoStack.pop_front();
        }
    }

    /// Reports whether undo() has a command to reverse so UI can disable undo
    /// controls instead of invoking a no-op on an empty history.
    bool canUndo() const
    {
        return !undoStack.empty();
    }

    /// Reports whether redo() has a command to replay so UI can disable redo
    /// controls until the user has undone something.
    bool canRedo() const
    {
        return !redoStack.empty();
    }

private:
    std::deque<std::unique_ptr<Command>> undoStack;
    std::deque<std::unique_ptr<Command>> redoStack;
};

#ifdef ELEC3D_TEST_COMMANDS

#include <cassert>

struct CounterCommand : public Command {
    int& counter;
    int delta;

    /// Captures a counter reference and a delta so the test can prove commands
    /// remember enough state to apply and reverse themselves later.
    CounterCommand(int& c, int d) : counter(c), delta(d) {}

    /// Adds delta to the counter so CommandHistory::push() and redo() have a
    /// visible effect that can be checked with a simple integer assertion.
    void execute() override { counter += delta; }

    /// Subtracts delta from the counter so undo() precisely reverses execute().
    void undo() override { counter -= delta; }
};

/// Compiles only when ELEC3D_TEST_COMMANDS is enabled and checks the smallest
/// undo/redo story: push 3 commands, undo 2, redo 1; expected final counter is 1.
inline void TestCommandHistory()
{
    int counter = 0;
    CommandHistory history;
    history.push(std::make_unique<CounterCommand>(counter, 2));
    history.push(std::make_unique<CounterCommand>(counter, -1));
    history.push(std::make_unique<CounterCommand>(counter, 5));
    history.undo();
    history.undo();
    history.redo();
    assert(counter == 1);
}

#endif

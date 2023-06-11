package message

import "sync"

type targetId uint64

var (
	lastID targetId
	idMtx  sync.Mutex
)

// Return the next ID we should use
func nextID() targetId {
	idMtx.Lock()
	defer idMtx.Unlock()
	lastID++
	return lastID
}

type Target struct {
	id targetId
}

func NewTarget() Target {
	return Target{id: nextID()}
}

func (t *Target) IsMine(t1 Target) bool {
	return t1.id == t.id
}

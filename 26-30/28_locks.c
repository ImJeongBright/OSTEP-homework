#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h> // for sched_yield

// 공통 상수 및 전역 변수
#define NUM_THREADS 4
#define NUM_ITERATIONS 100000

volatile int counter = 0; // 공유 자원

/* ============================================================
 * 1. Test-And-Set (Spin Lock)
 * 문서 참조: [cite: 221-271]
 * 하드웨어 원자적 명령어인 TestAndSet을 사용하여 구현.
 * x86에서는 xchg 명령어나 GCC 내장 함수 사용.
 * ============================================================ */
typedef struct {
    volatile int flag;
} spinlock_t;

void spinlock_init(spinlock_t *lock) {
    lock->flag = 0;
}

void spinlock_lock(spinlock_t *lock) {
    // __sync_lock_test_and_set은 이전 값을 반환하고 값을 1로 설정함
    // 반환값이 1이면 누군가 락을 소유 중이므로 계속 회전(Spin)
    while (__sync_lock_test_and_set(&lock->flag, 1) == 1) {
        // Spin-wait
    }
}

void spinlock_unlock(spinlock_t *lock) {
    // 락 해제 (0으로 설정)
    __sync_lock_release(&lock->flag); 
}

/* ============================================================
 * 2. Compare-And-Swap (Spin Lock)
 * 문서 참조: [cite: 314-343]
 * 예상값(0)과 일치하면 새로운 값(1)으로 교체.
 * ============================================================ */
typedef struct {
    volatile int flag;
} cas_lock_t;

void cas_init(cas_lock_t *lock) {
    lock->flag = 0;
}

void cas_lock(cas_lock_t *lock) {
    // __sync_val_compare_and_swap(ptr, old, new)
    // flag가 0이면 1로 바꾸고 0을 반환 -> 루프 탈출
    // flag가 1이면 변경 실패하고 1을 반환 -> 루프 반복
    while (__sync_val_compare_and_swap(&lock->flag, 0, 1) == 1) {
        // Spin-wait
    }
}

void cas_unlock(cas_lock_t *lock) {
    lock->flag = 0;
}

/* ============================================================
 * 3. Ticket Lock (Fetch-And-Add)
 * 문서 참조: [cite: 466-514]
 * 공정성(Fairness)을 보장하는 락.
 * ============================================================ */
typedef struct {
    volatile int ticket;
    volatile int turn;
} ticket_lock_t;

void ticket_init(ticket_lock_t *lock) {
    lock->ticket = 0;
    lock->turn = 0;
}

void ticket_lock(ticket_lock_t *lock) {
    // 내 번호표를 뽑음 (Atomic Fetch-And-Add)
    int myturn = __sync_fetch_and_add(&lock->ticket, 1);
    
    // 내 차례가 올 때까지 대기
    while (lock->turn != myturn) {
        // Spin-wait
    }
}

void ticket_unlock(ticket_lock_t *lock) {
    // 차례를 다음 사람으로 넘김
    // 문서에서는 단순 증가시키지만 원자적 연산 권장
    __sync_fetch_and_add(&lock->turn, 1);
}

/* ============================================================
 * 4. Yield Lock (Spin with Yield)
 * 문서 참조: [cite: 541-563]
 * 회전 중 CPU를 양보하여 성능 저하 방지 시도.
 * ============================================================ */
typedef struct {
    volatile int flag;
} yield_lock_t;

void yield_init(yield_lock_t *lock) {
    lock->flag = 0;
}

void yield_lock(yield_lock_t *lock) {
    while (__sync_lock_test_and_set(&lock->flag, 1) == 1) {
        sched_yield(); // CPU 양보 
    }
}

void yield_unlock(yield_lock_t *lock) {
    __sync_lock_release(&lock->flag);
}

/* ============================================================
 * 5. Queue Lock (Sleeping Lock) - POSIX Simulation
 * 문서 참조: [cite: 582-669]
 * 문서의 park()/unpark() 및 Solaris 전용 코드를 
 * POSIX pthread_cond로 재해석하여 구현.
 * guard(spinlock)로 내부 상태 보호 + 대기열(cond) 사용.
 * ============================================================ */
typedef struct {
    int flag;             // 락 상태 (0: free, 1: held)
    int guard;            // 내부 보호용 스핀락
    pthread_cond_t cond;  // 큐 역할 (waiting channel)
    pthread_mutex_t q_mutex; // cond와 함께 사용할 뮤텍스 (POSIX 요구사항)
} queue_lock_t;

void queue_lock_init(queue_lock_t *m) {
    m->flag = 0;
    m->guard = 0;
    pthread_cond_init(&m->cond, NULL);
    pthread_mutex_init(&m->q_mutex, NULL);
}

void queue_lock(queue_lock_t *m) {
    // 1. Guard 획득 (Spin)
    while (__sync_lock_test_and_set(&m->guard, 1) == 1);

    if (m->flag == 0) {
        m->flag = 1; // 락 획득
        m->guard = 0; // Guard 해제
    } else {
        // 락 획득 실패 시 대기열에 추가하고 잠들기
        // POSIX cond_wait는 mutex가 필요하므로 여기서 lock
        pthread_mutex_lock(&m->q_mutex);
        m->guard = 0; // 잠들기 전 Guard 해제 (중요)
        
        // 문서의 park()에 해당. signal이 올 때까지 대기.
        // 실제로는 spurious wakeup 때문에 while로 감싸야 하나, 
        // 문서의 로직(직접 깨우기)을 따르기 위해 단순 wait 사용.
        // 하지만 여기서는 안전을 위해 flag 체크 루프 사용.
        while (m->flag == 1) {
             pthread_cond_wait(&m->cond, &m->q_mutex);
        }
        m->flag = 1; // 깨어난 후 락 획득 표시
        pthread_mutex_unlock(&m->q_mutex);
    }
}

void queue_unlock(queue_lock_t *m) {
    // 1. Guard 획득
    while (__sync_lock_test_and_set(&m->guard, 1) == 1);

    // 대기자가 있는지 확인하는 로직은 POSIX cond에서 추상화되어 있음.
    // 문서 로직: 큐가 비어있으면 flag=0, 아니면 unpark()
    
    // POSIX 단순화: 
    // 항상 signal을 보내고 flag를 0으로 만듦. 
    // (정확한 구현은 cond 내부 대기자 수를 알 수 없으므로 
    //  flag를 0으로 만들고 signal을 보내 경쟁하게 하거나, 
    //  명시적 큐 자료구조가 필요함. 여기서는 기능적 등가 구현.)
    
    m->flag = 0;
    pthread_mutex_lock(&m->q_mutex);
    pthread_cond_signal(&m->cond); // unpark() [cite: 668]
    pthread_mutex_unlock(&m->q_mutex);
    
    m->guard = 0;
}

/* ============================================================
 * 메인 테스트 하네스
 * ============================================================ */

// 락 인스턴스
spinlock_t spin_lock_var;
cas_lock_t cas_lock_var;
ticket_lock_t ticket_lock_var;
yield_lock_t yield_lock_var;
queue_lock_t queue_lock_var;

// 테스트할 락 타입 선택
enum LockType { SPIN, CAS, TICKET, YIELD, QUEUE };
int current_test_type = SPIN; 

void* worker(void* arg) {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        switch(current_test_type) {
            case SPIN:
                spinlock_lock(&spin_lock_var);
                counter++;
                spinlock_unlock(&spin_lock_var);
                break;
            case CAS:
                cas_lock(&cas_lock_var);
                counter++;
                cas_unlock(&cas_lock_var);
                break;
            case TICKET:
                ticket_lock(&ticket_lock_var);
                counter++;
                ticket_unlock(&ticket_lock_var);
                break;
            case YIELD:
                yield_lock(&yield_lock_var);
                counter++;
                yield_unlock(&yield_lock_var);
                break;
            case QUEUE:
                queue_lock(&queue_lock_var);
                counter++;
                queue_unlock(&queue_lock_var);
                break;
        }
    }
    return NULL;
}

void run_test(const char* name, int type) {
    pthread_t threads[NUM_THREADS];
    counter = 0;
    current_test_type = type;

    // 초기화
    if (type == SPIN) spinlock_init(&spin_lock_var);
    if (type == CAS) cas_init(&cas_lock_var);
    if (type == TICKET) ticket_init(&ticket_lock_var);
    if (type == YIELD) yield_init(&yield_lock_var);
    if (type == QUEUE) queue_lock_init(&queue_lock_var);

    printf("Testing %s...\n", name);

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("Result: %d (Expected: %d)\n\n", counter, NUM_THREADS * NUM_ITERATIONS);
}

int main() {
    run_test("Test-And-Set Spin Lock", SPIN);
    run_test("Compare-And-Swap Lock", CAS);
    run_test("Ticket Lock", TICKET);
    run_test("Yield Lock", YIELD);
    run_test("Queue Lock (Simulated)", QUEUE);

    return 0;
}
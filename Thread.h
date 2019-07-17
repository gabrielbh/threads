//
// Created by hillelkh on 5/9/18.
//

#ifndef EX2_THREAD_H
#define EX2_THREAD_H



class Thread {
public:

    /**
     * Default constructor
     */
    Thread();

    /**
     * Constructor
     */
    Thread(int id, int state, void (*f)(void));

    /**
     * ID getter
     */
    int get_id();

    /**
     * Sets the state
     */
    void set_state(int state);


    /**
     * Stack getter
     */
    char* get_stack();

    /**
     * Sync getter
     */
    bool* get_syncs();

    /**
     * Returns total number of quantums of the thread
     */
    int get_quantums();

    /**
     * Adds quantums
     */
    void add_quantums();

    /**
     * Sync Setter
     */
    void set_syncs(int id);

private:

    int id;


    int state;

    void (*f)(void);


    bool syncs [100] = {false};


    char stack [4096];

    int quantums = 0;


};


#endif //EX2_THREAD_H
